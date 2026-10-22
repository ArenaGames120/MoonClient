/* MoonClient: Voice AI assistant. Microphone capture (SDL), local Whisper
 * speech recognition (data/moon/voice/model.bin, offline, no keys), online
 * LLM answer, Google TTS spoken back in-game as wavpack samples. */

#include "moonvoice.h"

#include <base/log.h>
#include <base/log_color.h>
#include <base/mem.h>
#include <base/str.h>
#include <base/time.h>
#include <base/types.h>

#include <engine/console.h>
#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <SDL.h>

#include <game/client/components/chat.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <engine/external/json-parser/json.h>

#define MINIMP3_IMPLEMENTATION
#include <engine/external/minimp3/minimp3.h>

#include <wavpack.h>

#include <engine/external/whisper/whisper.h>

#include <algorithm>
#include <cstdint>

CMoonVoice::CWhisperCtx CMoonVoice::ms_WhisperCtx;

static void MoonTrim(char *pText)
{
	str_utf8_trim_right(pText);
	const char *pStart = pText;
	while(*pStart == ' ' || *pStart == '\n' || *pStart == '\r' || *pStart == '\t')
		pStart++;
	if(pStart != pText)
		mem_move(pText, pStart, str_length(pStart) + 1);
}

static const char *const g_pVoiceSystemPrompt =
	"Ты дружелюбный голосовой ИИ-помощник внутри игры DDrace Network (DDNet, мод MoonClient). "
	"Отвечай живым разговорным языком на русском, очень кратко - максимум 80 слов, без списков и разметки. "
	"Помогаешь вопросами об игре DDrace и обычными бытовыми вопросами игрока.";

///////////////////////////////////////////////////////////////////////////////

CMoonVoice::CVoiceJob::CVoiceJob(const std::vector<float> &vPcm, IStorage *pStorage, const char *pLanguage) :
	m_vPcm(vPcm),
	m_pStorage(pStorage)
{
	str_copy(m_aLanguage, pLanguage, sizeof(m_aLanguage));
}

void CMoonVoice::CVoiceJob::Run()
{
	std::lock_guard<std::mutex> Lock(ms_WhisperCtx.Mutex);

	if(!ms_WhisperCtx.pCtx)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		m_pStorage->GetBinaryPath("data/moon/voice/model.bin", aPath, sizeof(aPath));
		ms_WhisperCtx.pCtx = whisper_init_from_file(aPath);
		if(!ms_WhisperCtx.pCtx)
		{
			str_copy(m_aError, "notfound", sizeof(m_aError));
			return;
		}
	}
	whisper_context *pCtx = (whisper_context *)ms_WhisperCtx.pCtx;

	whisper_full_params Params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
	Params.print_progress = false;
	Params.print_special = false;
	Params.print_realtime = false;
	Params.print_timestamps = false;
	Params.translate = false;
	Params.language = m_aLanguage;
	Params.n_threads = 4;
	Params.no_context = true;
	Params.single_segment = false;

	if(whisper_full(pCtx, Params, m_vPcm.data(), (int)m_vPcm.size()) != 0)
	{
		str_copy(m_aError, "whisperfail", sizeof(m_aError));
		return;
	}

	const int NumSegments = whisper_full_n_segments(pCtx);
	const int MaxText = (int)sizeof(m_aResult) - 2;
	int Cursor = 0;
	for(int i = 0; i < NumSegments && Cursor < MaxText; i++)
	{
		const char *pSegment = whisper_full_get_segment_text(pCtx, i);
		if(!pSegment)
			continue;
		while(*pSegment && (*pSegment == ' ' || *pSegment == '\n' || *pSegment == '\r'))
			pSegment++;
		if(*pSegment == '\0')
			continue;
		const int AppendLen = str_length(pSegment);
		if(Cursor > 0 && Cursor + 1 < MaxText)
			m_aResult[Cursor++] = ' ';
		str_truncate(m_aResult + Cursor, sizeof(m_aResult) - Cursor, pSegment, AppendLen);
		Cursor += AppendLen;
	}
	m_aResult[Cursor < (int)sizeof(m_aResult) - 1 ? Cursor : (int)sizeof(m_aResult) - 1] = '\0';
}

CMoonVoice::CTtsDecodeJob::CTtsDecodeJob(ISound *pSound, std::vector<unsigned char> &&vMp3, const char *pName) :
	m_pSound(pSound),
	m_vMp3(std::move(vMp3))
{
	str_copy(m_aName, pName, sizeof(m_aName));
}

void CMoonVoice::CTtsDecodeJob::Run()
{
	// 1) decode the google tts mp3 into mono int16 pcm
	mp3dec_t Decoder;
	mp3dec_init(&Decoder);
	mp3dec_frame_info_t FrameInfo;
	mem_zero(&FrameInfo, sizeof(FrameInfo));

	std::vector<int16_t> vPcm;
	vPcm.reserve(24000 * 16);
	size_t Offset = 0;
	while(Offset < m_vMp3.size())
	{
		int16_t aFrame[MINIMP3_MAX_SAMPLES_PER_FRAME];
		const int Samples = mp3dec_decode_frame(&Decoder, m_vMp3.data() + Offset, (int)(m_vMp3.size() - Offset), aFrame, &FrameInfo);
		if(Samples > 0 && FrameInfo.hz > 0)
		{
			if(FrameInfo.channels == 2)
			{
				for(int i = 0; i < Samples; i++)
					vPcm.push_back((int16_t)(((int)aFrame[2 * i] + (int)aFrame[2 * i + 1]) / 2));
			}
			else
			{
				vPcm.insert(vPcm.end(), aFrame, aFrame + Samples);
			}
		}
		if(FrameInfo.frame_bytes <= 0)
			break;
		Offset += FrameInfo.frame_bytes;
	}

	if(vPcm.empty() || FrameInfo.hz == 0)
	{
		str_copy(m_aError, "decode", sizeof(m_aError));
		return;
	}

	// 2) encode as a wavpack stream in memory (the game sound engine's format)
	std::vector<unsigned char> vWvBlob;
	vWvBlob.reserve(vPcm.size() * 2 + 4096);
	auto WriteCallback = [](void *pId, void *pData, int32_t Bcount) -> int32_t {
		if(Bcount <= 0 || !pData)
			return 0;
		auto *pVec = static_cast<std::vector<unsigned char> *>(pId);
		pVec->insert(pVec->end(), static_cast<const unsigned char *>(pData), static_cast<const unsigned char *>(pData) + Bcount);
		return Bcount;
	};

	WavpackContext *pWriter = WavpackOpenFileOutput(WriteCallback, &vWvBlob, nullptr);
	if(!pWriter)
	{
		str_copy(m_aError, "wvinit", sizeof(m_aError));
		return;
	}

	WavpackConfig Config;
	mem_zero(&Config, sizeof(Config));
	Config.bits_per_sample = 16;
	Config.bytes_per_sample = 2;
	Config.num_channels = 1;
	Config.sample_rate = FrameInfo.hz;

	if(!WavpackSetConfiguration64(pWriter, &Config, (int64_t)vPcm.size(), nullptr))
	{
		WavpackCloseFile(pWriter);
		str_copy(m_aError, "wvcfg", sizeof(m_aError));
		return;
	}
	if(!WavpackPackInit(pWriter))
	{
		WavpackCloseFile(pWriter);
		str_copy(m_aError, "wvpack", sizeof(m_aError));
		return;
	}

	// wavpack wants 32 bit values, our samples live in the upper half
	std::vector<int32_t> vBuf(2048);
	for(size_t i = 0; i < vPcm.size(); i += vBuf.size())
	{
		const size_t Count = std::min<size_t>(vBuf.size(), vPcm.size() - i);
		for(size_t s = 0; s < Count; s++)
			vBuf[s] = ((int32_t)vPcm[i + s]) << 16;
		WavpackPackSamples(pWriter, vBuf.data(), (uint32_t)Count);
	}
	WavpackFlushSamples(pWriter);
	WavpackCloseFile(pWriter);

	// 3) load into the sound engine (from a job thread, like the game sound loading)
	if(vWvBlob.empty())
	{
		str_copy(m_aError, "wvempty", sizeof(m_aError));
		return;
	}
	m_SampleId = m_pSound->LoadWVFromMem(vWvBlob.data(), (unsigned)vWvBlob.size(), false, "moonvoice tts");
};

///////////////////////////////////////////////////////////////////////////////

void CMoonVoice::OnConsoleInit()
{
	Console()->Register("moon_voice", "", CFGFLAG_CLIENT, ConVoiceTalk, this, "MoonClient: talk to the AI assistant with your voice (press again to stop)");
	Console()->Register("moon_voice_stop", "", CFGFLAG_CLIENT, ConVoiceStop, this, "MoonClient: stop the voice recording without using it");
}

void CMoonVoice::ConVoiceTalk(IConsole::IResult *pResult, void *pUserData)
{
	CMoonVoice *pSelf = static_cast<CMoonVoice *>(pUserData);

	if(g_Config.m_McVoiceAsst == 0)
	{
		pSelf->Say("", "Голосовой ассистент выключен (включи в настройках MoonClient)");
		return;
	}

	switch(pSelf->m_State)
	{
	case STATE_IDLE:
		pSelf->StartRecording();
		break;
	case STATE_RECORDING:
		pSelf->StopRecording(false);
		break;
	default:
		pSelf->Say("", "Ассистент занят, секунду");
		break;
	}
}

void CMoonVoice::ConVoiceStop(IConsole::IResult *pResult, void *pUserData)
{
	CMoonVoice *pSelf = static_cast<CMoonVoice *>(pUserData);
	if(pSelf->m_State == STATE_RECORDING)
		pSelf->StopRecording(true);
	else if(pSelf->m_State != STATE_IDLE)
	{
		if(pSelf->m_pAskRequest)
			pSelf->m_pAskRequest->Abort();
		if(pSelf->m_pTtsRequest)
			pSelf->m_pTtsRequest->Abort();
		pSelf->OnReset();
	}
}

void CMoonVoice::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_OFFLINE || NewState == IClient::STATE_CONNECTING)
	{
		if(m_State == STATE_RECORDING)
			StopRecording(true);
	}
}

void CMoonVoice::OnReset()
{
	ClearSpeakingState();
	ClearQuestionState();
	ClearTranscribeState();
	m_State = STATE_IDLE;
	m_HeardVoice = false;
}

void CMoonVoice::ClearTranscribeState()
{
	m_pVoiceJob = nullptr;
}

void CMoonVoice::ClearQuestionState()
{
	m_pAskRequest = nullptr;
	m_vConversation.clear();
}

void CMoonVoice::ClearSpeakingState()
{
	if(m_pTtsRequest)
		m_pTtsRequest->Abort();
	m_pTtsRequest = nullptr;
	m_pTtsDecodeJob = nullptr;
	m_vTtsChunks.clear();
	m_TtsChunkIndex = 0;
	if(m_SpeakCurrentSample != -1)
	{
		Sound()->UnloadSample(m_SpeakCurrentSample);
		m_SpeakCurrentSample = -1;
	}
	m_vSpeakQueue.clear();
}

///////////////////////////////////////////////////////////////////////////////

void CMoonVoice::StartRecording()
{
	if(m_AudioDev == 0)
	{
		const int NumCapture = SDL_GetNumAudioDevices(1);
		if(NumCapture == 0)
		{
			Say("", "Микрофон не найден. Разреши доступ: Параметры Windows → Конфиденциальность → Микрофон (включи для классических приложений)");
			return;
		}

		SDL_AudioSpec Want;
		mem_zero(&Want, sizeof(Want));
		Want.freq = 16000;
		Want.format = AUDIO_S16SYS;
		Want.channels = 1;
		Want.samples = 4096;
		Want.callback = nullptr;

		// try every capture device in turn: the default one can be a broken
		// virtual mic (e.g. voicemod) while a working real mic sits next to it
		for(int i = 0; i < NumCapture && m_AudioDev == 0; i++)
		{
			const char *pName = SDL_GetAudioDeviceName(i, 1);
			if(!pName)
				continue;

			SDL_AudioSpec Have;
			mem_zero(&Have, sizeof(Have));
			m_AudioDev = SDL_OpenAudioDevice(pName, 1, &Want, &Have, 0);
			if(m_AudioDev != 0)
				str_copy(m_aMicName, pName, sizeof(m_aMicName));
		}

		if(m_AudioDev == 0)
		{
			char aErr[256];
			str_copy(aErr, SDL_GetError(), sizeof(aErr));
			char aInfo[576];
			str_format(aInfo, sizeof(aInfo), "Микрофоны есть (%d шт), но открыть не удалось: %s. Проверь, что устройство «Microphone (High Definition Audio Device)» включено и не занято Voicemod/Discord.", NumCapture, aErr);
			Say("", aInfo);
			return;
		}

		char aMicInfo[320];
		str_format(aMicInfo, sizeof(aMicInfo), "Микрофон подключен: %s", m_aMicName);
		Say("", aMicInfo);
	}

	m_vRecorded.clear();
	m_vRecorded.reserve(16000 * 13);
	m_RecordStart = Client()->LocalTime();
	m_LastVoice = m_RecordStart;
	m_HeardVoice = false;
	m_MaxSeenAmp = 0;
	str_copy(m_aSpokenLanguage, g_Config.m_McVoiceLang, sizeof(m_aSpokenLanguage));

	SDL_PauseAudioDevice(m_AudioDev, 0);
	m_State = STATE_RECORDING;
}

void CMoonVoice::StopRecording(bool Cancel)
{
	if(m_State != STATE_RECORDING)
		return;
	SDL_PauseAudioDevice(m_AudioDev, 1);
	m_State = STATE_IDLE;
	(void)m_RecordStart;

	const float Seconds = m_vRecorded.size() / 16000.0f;
	if(Cancel)
	{
		m_vRecorded.clear();
		Say("", "Запись отменена");
		return;
	}

	if(!m_HeardVoice || Seconds < 0.4f)
	{
		m_vRecorded.clear();
		char aInfo[256];
		str_format(aInfo, sizeof(aInfo), "Не услышал тебя: пик громкости %d из 32768, длина %.1f сек. Скажи громче, включи микрофон или поднеси его ближе.", m_MaxSeenAmp, (double)Seconds);
		Say("", aInfo);
		return;
	}

	StartTranscription();
}

void CMoonVoice::UpdateRecording()
{
	if(!m_AudioDev)
	{
		m_State = STATE_IDLE;
		return;
	}

	unsigned char aBuf[8192];
	Uint32 Got;
	while((Got = SDL_DequeueAudio(m_AudioDev, aBuf, sizeof(aBuf))) > 0)
	{
		const int16_t *pData = (const int16_t *)aBuf;
		const size_t NumSamples = Got / 2;
		for(size_t i = 0; i < NumSamples; i++)
		{
			const int Amp = (pData[i] < 0 ? -pData[i] : pData[i]);
			if(Amp > m_MaxSeenAmp)
				m_MaxSeenAmp = Amp;
		}
		m_vRecorded.insert(m_vRecorded.end(), pData, pData + NumSamples);
	}

	// loudness of the last ~0.5 s drives the silence detection
	const size_t Window = std::min<size_t>(m_vRecorded.size(), 8000);
	int MaxAmp = 0;
	for(size_t i = m_vRecorded.size() - Window; i < m_vRecorded.size(); i++)
	{
		const int Amp = (m_vRecorded[i] < 0 ? -m_vRecorded[i] : m_vRecorded[i]);
		if(Amp > MaxAmp)
			MaxAmp = Amp;
	}
	const int64_t Now = Client()->LocalTime();
	if(MaxAmp > 1400)
	{
		m_HeardVoice = true;
		m_LastVoice = Now;
	}

	const float Seconds = m_vRecorded.size() / 16000.0f;
	if(m_HeardVoice && Now - m_LastVoice >= 0.9 * (double)time_freq())
	{
		StopRecording(false);
		return;
	}
	if(Seconds >= 12.0f)
	{
		StopRecording(false);
		return;
	}
}

void CMoonVoice::StartTranscription()
{
	std::vector<float> vPcm;
	vPcm.resize(m_vRecorded.size());
	for(size_t i = 0; i < m_vRecorded.size(); i++)
		vPcm[i] = m_vRecorded[i] / 32768.0f;
	m_vRecorded.clear();

	m_pVoiceJob = std::make_shared<CVoiceJob>(vPcm, Storage(), m_aSpokenLanguage);
	Engine()->AddJob(m_pVoiceJob);
	m_State = STATE_TRANSCRIBING;
}

void CMoonVoice::UpdateTranscription()
{
	if(!m_pVoiceJob)
	{
		m_State = STATE_IDLE;
		return;
	}
	if(m_pVoiceJob->State() != IJob::STATE_DONE)
		return;

	char aQuestion[MAX_CHAT_LENGTH];
	aQuestion[0] = '\0';
	if(m_pVoiceJob->m_aError[0] != '\0')
	{
		if(str_comp(m_pVoiceJob->m_aError, "notfound") == 0)
			Say("", "Файл модели ИИ не найден (data/moon/voice/model.bin)");
		else
			Say("", "Я не смог разобрать речь — не получилось");
		ClearTranscribeState();
		m_State = STATE_IDLE;
		return;
	}

	str_copy(aQuestion, m_pVoiceJob->m_aResult, MAX_CHAT_LENGTH);
	MoonTrim(aQuestion);
	ClearTranscribeState();

	if(str_length(aQuestion) < 2)
	{
		Say("", "Слишком коротко, я не понял — скажи полнее");
		m_State = STATE_IDLE;
		return;
	}

	Say("[Ты сказал]", aQuestion);
	AskAssistant(aQuestion);
}

///////////////////////////////////////////////////////////////////////////////

void CMoonVoice::AskAssistant(const char *pQuestion)
{
	{
		CMsg Msg;
		Msg.m_Assistant = false;
		str_copy(Msg.m_aText, pQuestion, sizeof(Msg.m_aText));
		m_vConversation.push_back(Msg);
		while(m_vConversation.size() > 6)
			m_vConversation.erase(m_vConversation.begin());
	}

	// openai-styled POST body for text.pollinations.ai (free, no key)
	char aBody[1024];
	str_copy(aBody, "{\"model\":\"openai\",\"messages\":[{\"role\":\"system\",\"content\":\"", sizeof(aBody));
	char aEscaped[768];
	EscapeJson(aEscaped, sizeof(aEscaped), g_pVoiceSystemPrompt);
	str_append(aBody, aEscaped, sizeof(aBody));
	for(const CMsg &Msg : m_vConversation)
	{
		EscapeJson(aEscaped, sizeof(aEscaped), Msg.m_aText);
		char aPart[768];
		str_format(aPart, sizeof(aPart), ",{\"role\":\"%s\",\"content\":\"%s\"}", Msg.m_Assistant ? "assistant" : "user", aEscaped);
		if(str_length(aBody) + str_length(aPart) < (int)sizeof(aBody) - 8)
			str_append(aBody, aPart, sizeof(aBody));
	}
	str_append(aBody, "]}", sizeof(aBody));

	m_pAskRequest = CreateHttpRequest("https://text.pollinations.ai/openai");
	m_pAskRequest->Post((unsigned char *)aBody, str_length(aBody));
	m_pAskRequest->HeaderString("Content-Type", "application/json");
	m_pAskRequest->Timeout(CTimeout{4000, 20000, 400, 3});
	m_pAskRequest->LogProgress(HTTPLOG::NONE);
	Http()->Run(m_pAskRequest);
	m_State = STATE_ASKING;
}

void CMoonVoice::ExtractStreamResult(const char *pJsonBody, char *pOut, unsigned OutSize)
{
	pOut[0] = '\0';
	json_value *pJson = JsonParse(pJsonBody, str_length(pJsonBody));
	if(!pJson)
		return;
	const json_value *pChoices = json_object_get(pJson, "choices");
	if(pChoices && pChoices->type == json_array && pChoices->u.array.length >= 1)
	{
		const json_value *pMsg = json_object_get(pChoices->u.array.values[0], "message");
		const json_value *pContent = pMsg ? json_object_get(pMsg, "content") : json_object_get(pChoices->u.array.values[0], "text");
		if(pContent && pContent->type == json_string)
			str_copy(pOut, pContent->u.string.ptr, OutSize);
	}
	json_value_free(pJson);
}

void CMoonVoice::UpdateAsking()
{
	if(!m_pAskRequest)
	{
		m_State = STATE_IDLE;
		return;
	}
	if(!m_pAskRequest->Done())
		return;

	char aReply[1024];
	aReply[0] = '\0';
	if(m_pAskRequest->State() == EHttpState::DONE && m_pAskRequest->StatusCode() == 200)
	{
		unsigned char *pBody = nullptr;
		size_t BodyLength = 0;
		m_pAskRequest->Result(&pBody, &BodyLength);
		if(pBody)
		{
			char aJson[4096];
			str_truncate(aJson, sizeof(aJson), (const char *)pBody, (int)std::min<size_t>(sizeof(aJson) - 1, BodyLength));
			ExtractStreamResult(aJson, aReply, sizeof(aReply));
			free(pBody);
		}
	}
	m_pAskRequest = nullptr;

	MoonTrim(aReply);
	if(str_length(aReply) < 1)
	{
		Say("", "ИИ не отвечает, попробуй позже");
		m_State = STATE_IDLE;
		return;
	}

	{
		CMsg Msg;
		Msg.m_Assistant = true;
		str_copy(Msg.m_aText, aReply, sizeof(Msg.m_aText));
		m_vConversation.push_back(Msg);
		while(m_vConversation.size() > 6)
			m_vConversation.erase(m_vConversation.begin());
	}

	Say("[ИИ]", aReply);

	// split for google tts URL cap (~200 chars per request)
	char aClean[768];
	str_copy(aClean, aReply, sizeof(aClean));
	size_t Offset = 0;
	while(Offset < (size_t)str_length(aClean) && m_vTtsChunks.size() < 4)
	{
		size_t Take = str_length(aClean) - Offset;
		if(Take > 160)
		{
			size_t Space = Offset + 160;
			while(Space > Offset && aClean[Space] != ' ' && aClean[Space] != ',')
				Space--;
			if(Space > Offset)
				Take = Space - Offset + 1;
			else
				Take = 160;
		}
		char aChunk[256];
		str_truncate(aChunk, sizeof(aChunk), aClean + Offset, (int)Take);
		m_vTtsChunks.emplace_back(aChunk);
		Offset += Take;
	}

	if(m_vTtsChunks.empty())
		m_State = STATE_IDLE;
	else
	{
		m_TtsChunkIndex = 0;
		m_State = STATE_FETCHING_TTS;
	}
}

///////////////////////////////////////////////////////////////////////////////

void CMoonVoice::FetchNextTtsChunk()
{
	const std::string &Chunk = m_vTtsChunks[m_TtsChunkIndex];
	char aEscaped[512];
	EscapeUrl(aEscaped, sizeof(aEscaped), Chunk.c_str());

	char aUrl[768];
	str_format(aUrl, sizeof(aUrl), "https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=%s&q=%s", g_Config.m_McVoiceLang, aEscaped);

	m_pTtsRequest = CreateHttpRequest(aUrl);
	m_pTtsRequest->HeaderString("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36");
	m_pTtsRequest->Timeout(CTimeout{4000, 15000, 400, 3});
	m_pTtsRequest->LogProgress(HTTPLOG::NONE);
	Http()->Run(m_pTtsRequest);
}

bool CMoonVoice::DecodeChunk()
{
	unsigned char *pBody = nullptr;
	size_t BodyLength = 0;
	m_pTtsRequest->Result(&pBody, &BodyLength);
	const bool Success = m_pTtsRequest->State() == EHttpState::DONE && m_pTtsRequest->StatusCode() == 200;
	m_pTtsRequest = nullptr;
	m_TtsChunkIndex++;

	if(Success && pBody && BodyLength > 128)
	{
		std::vector<unsigned char> vMp3;
		vMp3.assign(pBody, pBody + BodyLength);
		free(pBody);
		m_pTtsDecodeJob = std::make_shared<CTtsDecodeJob>(Sound(), std::move(vMp3), "moon tts");
		Engine()->AddJob(m_pTtsDecodeJob);
		return true;
	}
	if(pBody)
		free(pBody);
	return false;
}

void CMoonVoice::UpdateSpeaking()
{
	// unload the finished current chunk
	if(m_SpeakCurrentSample != -1 && !Sound()->IsPlaying(m_SpeakCurrentSample))
	{
		Sound()->UnloadSample(m_SpeakCurrentSample);
		m_SpeakCurrentSample = -1;
	}

	// finished decode job -> sample queue
	if(m_pTtsDecodeJob && m_pTtsDecodeJob->State() == IJob::STATE_DONE)
	{
		if(m_pTtsDecodeJob->m_SampleId >= 0)
			m_vSpeakQueue.push_back(m_pTtsDecodeJob->m_SampleId);
		m_pTtsDecodeJob = nullptr;
	}

	// tts http chain, one chunk at a time (google tts rate limits)
	if(!m_pTtsRequest && m_TtsChunkIndex < m_vTtsChunks.size())
	{
		FetchNextTtsChunk();
	}
	else if(m_pTtsRequest && m_pTtsRequest->Done())
	{
		DecodeChunk();
	}

	// play the next ready chunk
	if(m_SpeakCurrentSample == -1 && !m_vSpeakQueue.empty())
	{
		const int SampleId = m_vSpeakQueue.front();
		m_vSpeakQueue.erase(m_vSpeakQueue.begin());
		if(Sound()->IsSoundEnabled())
		{
			m_SpeakVoice = Sound()->Play(CSounds::CHN_GLOBAL, SampleId, 0, g_Config.m_McVoiceVolume / 100.0f);
			m_SpeakCurrentSample = SampleId;
		}
		else
			Sound()->UnloadSample(SampleId);
	}

	// done with everything?
	if(m_TtsChunkIndex >= m_vTtsChunks.size() && !m_pTtsRequest && !m_pTtsDecodeJob && m_vSpeakQueue.empty() && m_SpeakCurrentSample == -1)
	{
		ClearSpeakingState();
		m_State = STATE_IDLE;
	}
}

void CMoonVoice::Say(const char *pPrefix, const char *pText)
{
	char aLine[MAX_CHAT_LENGTH];
	str_copy(aLine, pText, sizeof(aLine));
	MoonTrim(aLine);
	if(str_length(aLine) < 1)
		return;

	char aFull[MAX_CHAT_LENGTH];
	if(pPrefix[0] != '\0')
		str_format(aFull, sizeof(aFull), "%s: %s", pPrefix, aLine);
	else
		str_copy(aFull, aLine, sizeof(aFull));
	GameClient()->m_Chat.Echo(aFull);
}

///////////////////////////////////////////////////////////////////////////////

void CMoonVoice::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	switch(m_State)
	{
	case STATE_RECORDING:
		UpdateRecording();
		break;
	case STATE_TRANSCRIBING:
		UpdateTranscription();
		break;
	case STATE_ASKING:
		UpdateAsking();
		break;
	case STATE_FETCHING_TTS:
	case STATE_SPEAKING:
		UpdateSpeaking();
		break;
	default:
		break;
	}

	if(g_Config.m_McVoiceAsst == 0)
		return;

	const char *pStatus = nullptr;
	switch(m_State)
	{
	case STATE_RECORDING: pStatus = "● ВЫ ГОВОРИТЕ — стоп: та же кнопка (пауза ~1 сек сама остановит)"; break;
	case STATE_TRANSCRIBING: pStatus = "● Распознаю речь..."; break;
	case STATE_ASKING: pStatus = "● Спрашиваю ИИ..."; break;
	case STATE_FETCHING_TTS: pStatus = "● Озвучиваю..."; break;
	case STATE_SPEAKING: pStatus = "● Отвечаю голосом..."; break;
	default:
		break;
	}
	if(!pStatus)
		return;

	const float Height = 300.0f;
	const float Width = Height * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(Width, Height);
	const float TextWidth = TextRender()->TextWidth(10.0f, pStatus, -1, Width);
	CTextCursor Cursor;
	Cursor.SetPosition(vec2((Width - TextWidth) / 2.0f, 58.0f));
	Cursor.m_FontSize = 10.0f;
	Cursor.m_LineWidth = Width;
	TextRender()->TextEx(&Cursor, pStatus, -1);
}