/* MoonClient: voice AI assistant — microphone -> local Whisper speech
 * recognition -> online LLM answer -> Google TTS spoken back in-game. */

#ifndef GAME_CLIENT_COMPONENTS_MOONVOICE_H
#define GAME_CLIENT_COMPONENTS_MOONVOICE_H

#include <base/vmath.h>

#include <engine/console.h>
#include <engine/http.h>
#include <engine/shared/jobs.h>
#include <engine/shared/protocol.h>
#include <engine/sound.h>

#include <game/client/component.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class CMoonVoice : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnReset() override;

private:
	enum
	{
		STATE_IDLE = 0, // doing nothing
		STATE_RECORDING, // mic capture cycling on the main thread
		STATE_TRANSCRIBING, // whisper job working
		STATE_ASKING, // LLM request in flight
		STATE_FETCHING_TTS, // fetching tts mp3 chunks
		STATE_SPEAKING, // chunks playing back
	};

	// lazily initialized shared whisper context (on the worker thread)
	struct CWhisperCtx
	{
		std::mutex Mutex;
		void *pCtx = nullptr; // whisper_context*
	};
	static CWhisperCtx ms_WhisperCtx;

	class CVoiceJob : public IJob
	{
	public:
		CVoiceJob(const std::vector<float> &vPcm, IStorage *pStorage, const char *pLanguage);
		void Run() override;
		const std::vector<float> m_vPcm;
		IStorage *m_pStorage;
		char m_aLanguage[8];
		char m_aResult[2048] = "";
		char m_aError[256] = "";
	};

	class CTtsDecodeJob : public IJob
	{
	public:
		CTtsDecodeJob(ISound *pSound, std::vector<unsigned char> &&vMp3, const char *pName);
		void Run() override;
		ISound *m_pSound;
		std::vector<unsigned char> m_vMp3;
		char m_aName[64];
		int m_SampleId = -1;
		char m_aError[256] = "";
	};

	static void ConVoiceTalk(IConsole::IResult *pResult, void *pUserData);
	static void ConVoiceStop(IConsole::IResult *pResult, void *pUserData);

	void StartRecording();
	void StopRecording(bool Cancel);
	void UpdateRecording();
	void StartTranscription();
	void UpdateTranscription();
	void AskAssistant(const char *pQuestion);
	void UpdateAsking();
	void UpdateSpeaking();
	void FetchNextTtsChunk();
	bool DecodeChunk();
	static void ExtractStreamResult(const char *pJsonBody, char *pOut, unsigned OutSize);
	void Say(const char *pPrefix, const char *pText);

	void ClearTranscribeState();
	void ClearSpeakingState();
	void ClearQuestionState();

	// recording (SDL capture, polled on the main thread)
	unsigned m_AudioDev = 0; // SDL_AudioDeviceID
	std::vector<int16_t> m_vRecorded;
	int64_t m_RecordStart = 0;
	int64_t m_LastVoice = 0;
	bool m_HeardVoice = false;
	int m_MaxSeenAmp = 0; // peak amplitude of the whole recording for diagnostics
	char m_aMicName[128]; // name of the capture device that successfully opened
	char m_aSpokenLanguage[8]; // copy of the config when the recording starts

	// transcription
	std::shared_ptr<CVoiceJob> m_pVoiceJob;

	// small conversation memory for the assistant
	struct CMsg
	{
		char m_aText[512];
		bool m_Assistant;
	};
	std::vector<CMsg> m_vConversation;
	std::shared_ptr<IHttpRequest> m_pAskRequest;

	// tts fetch/decode/play chain
	std::vector<std::string> m_vTtsChunks;
	size_t m_TtsChunkIndex = 0;
	std::shared_ptr<IHttpRequest> m_pTtsRequest;
	std::shared_ptr<CTtsDecodeJob> m_pTtsDecodeJob;
	std::vector<int> m_vSpeakQueue;
	int m_SpeakCurrentSample = -1;
	ISound::CVoiceHandle m_SpeakVoice;

	int m_State = STATE_IDLE;
};

#endif // GAME_CLIENT_COMPONENTS_MOONVOICE_H