/* MoonClient settings page */
#include "menus.h"

#include <engine/shared/config.h>

#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

void CMenus::RenderSettingsMoonClient(CUIRect MainView)
{
	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 90.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);

	CUIRect Label, Button, Left, Right;
	MainView.VSplitMid(&Left, &Right, 20.0f);

	const float ColorPickerLineSize = 25.0f;
	const float ColorPickerLabelSize = 13.0f;
	const float ColorPickerLineSpacing = 5.0f;

	static CButtonContainer s_OutlineColor;
	static CButtonContainer s_SelfHighlightColor;
	static CButtonContainer s_HookColor;

	// split off a row from the column, register it in the scroll region and return it
	auto Row = [&](CUIRect *pColumn, float Gap, float Height, CUIRect *pRow) {
		if(Gap > 0.0f)
			pColumn->HSplitTop(Gap, nullptr, pColumn);
		pColumn->HSplitTop(Height, pRow, pColumn);
		s_ScrollRegion.AddRect(*pRow);
	};

	auto LabelRow = [&](CUIRect *pColumn, const char *pText, float Gap = 10.0f) {
		CUIRect RowRect;
		Row(pColumn, Gap, 30.0f, &RowRect);
		Ui()->DoLabel(&RowRect, pText, 20.0f, TEXTALIGN_ML);
	};

	auto CheckRow = [&](CUIRect *pColumn, const void *pId, const char *pText, int *pValue, float Gap = 5.0f) {
		CUIRect RowRect;
		Row(pColumn, Gap, 20.0f, &RowRect);
		if(DoButton_CheckBox(pId, pText, *pValue, &RowRect))
			*pValue ^= 1;
	};

	auto SliderRow = [&](CUIRect *pColumn, const void *pId, int *pValue, const char *pText, int Min, int Max, float Gap = 5.0f) {
		CUIRect RowRect;
		Row(pColumn, Gap, 20.0f, &RowRect);
		Ui()->DoScrollbarOption(pId, pValue, &RowRect, pText, Min, Max);
	};

	// color picker rows consume multiple rows through DoLine_ColorPicker,
	// so register the consumed area of the parent column for scrolling
	auto PickerRow = [&](CUIRect *pColumn, CButtonContainer *pResetId, const char *pText, unsigned int *pColorValue, ColorRGBA DefaultColor, bool UseCheckBox, int *pCheckBoxValue, float Gap = 5.0f) {
		const CUIRect Before = *pColumn;
		if(Gap > 0.0f)
			pColumn->HSplitTop(Gap, nullptr, pColumn);
		DoLine_ColorPicker(pResetId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, pColumn, pText, pColorValue, DefaultColor, UseCheckBox, pCheckBoxValue, true);
		CUIRect Consumed = Before;
		Consumed.h = (Before.y + Before.h) - (pColumn->y + pColumn->h);
		s_ScrollRegion.AddRect(Consumed);
	};

	// tee visuals
	{
		LabelRow(&Left, "MoonClient", 10.0f);

		CheckRow(&Left, &g_Config.m_ClMotionBlur, Localize("Motion blur"), &g_Config.m_ClMotionBlur);
		SliderRow(&Left, &g_Config.m_ClMotionBlurStrength, &g_Config.m_ClMotionBlurStrength, Localize("Motion blur strength"), 0, 100);
		CheckRow(&Left, &g_Config.m_ClMotionBlurOthers, Localize("Motion blur for other players"), &g_Config.m_ClMotionBlurOthers);

		CheckRow(&Left, &g_Config.m_ClOutline, Localize("Custom tee outline"), &g_Config.m_ClOutline, 10.0f);
		PickerRow(&Left, &s_OutlineColor, Localize("Outline color"), &g_Config.m_ClOutlineColor, ColorRGBA(0.0f, 0.0f, 0.0f), false, nullptr);
		SliderRow(&Left, &g_Config.m_ClOutlineThickness, &g_Config.m_ClOutlineThickness, Localize("Outline thickness"), 1, 5);

		CheckRow(&Left, &g_Config.m_ClSelfOutline, Localize("Force outline on your own tee"), &g_Config.m_ClSelfOutline, 10.0f);
		PickerRow(&Left, &s_SelfHighlightColor, Localize("Self highlight (alpha = strength)"), &g_Config.m_ClSelfHighlight, ColorRGBA(0.0f, 0.0f, 0.0f), false, nullptr);

		CheckRow(&Left, &g_Config.m_ClHideOthersEmotes, Localize("Hide emotes of other players"), &g_Config.m_ClHideOthersEmotes, 10.0f);

		LabelRow(&Left, "Аим бот");
		CheckRow(&Left, &g_Config.m_ClAimAssist, "Зона FOV (цель в радиусе крюка)", &g_Config.m_ClAimAssist);
		SliderRow(&Left, &g_Config.m_ClAimAssistFov, &g_Config.m_ClAimAssistFov, "FOV (угол обзора)", 0, 180);
		CheckRow(&Left, &g_Config.m_ClAimAssistDrawFov, "Рисовать зону FOV", &g_Config.m_ClAimAssistDrawFov);
		CheckRow(&Left, &g_Config.m_ClAimAssistHook, "Хук по цели из зоны FOV", &g_Config.m_ClAimAssistHook);
		CheckRow(&Left, &g_Config.m_ClAimAssistLine, "Линия к цели", &g_Config.m_ClAimAssistLine);

		LabelRow(&Left, "Спин бот");
		CheckRow(&Left, &g_Config.m_ClSpinBot, "Спин бот (кручение прицела)", &g_Config.m_ClSpinBot);
		SliderRow(&Left, &g_Config.m_ClSpinBotSpeed, &g_Config.m_ClSpinBotSpeed, "Скорость (град/сек)", 30, 1440);

		LabelRow(&Left, "Чат");
		CheckRow(&Left, &g_Config.m_ClChatTranslate, "Перевод чата (ИИ)", &g_Config.m_ClChatTranslate);

		LabelRow(&Left, "Звук");
		CheckRow(&Left, &g_Config.m_McAsmr, "ASMR звуки (кастомный эмбиент)", &g_Config.m_McAsmr);
		SliderRow(&Left, &g_Config.m_McAsmrVolume, &g_Config.m_McAsmrVolume, "Громкость ASMR", 0, 100);

		LabelRow(&Left, "ИИ-ассистент");
		CheckRow(&Left, &g_Config.m_McVoiceAsst, "Голосовой ИИ-ассистент (клавишу привяжи к «moon_voice»)", &g_Config.m_McVoiceAsst);
		SliderRow(&Left, &g_Config.m_McVoiceVolume, &g_Config.m_McVoiceVolume, "Громкость голоса ассистента", 0, 100);

		LabelRow(&Left, "Оформление меню");
		CheckRow(&Left, &g_Config.m_McMenuTheme, "Тема «месяц и облака»", &g_Config.m_McMenuTheme);
		CheckRow(&Left, &g_Config.m_McStartClock, "Часы и дата на старт-меню", &g_Config.m_McStartClock);
	}

	// camera and effects
	{
		LabelRow(&Right, Localize("Camera and effects"), 10.0f);

		CheckRow(&Right, &g_Config.m_ClFocusMode, Localize("Focus mode (zoom on tee, clean screen)"), &g_Config.m_ClFocusMode);
		SliderRow(&Right, &g_Config.m_ClFocusZoom, &g_Config.m_ClFocusZoom, Localize("Focus mode zoom (10 = default)"), 0, 20);

		CheckRow(&Right, &g_Config.m_ClGores, Localize("Gores mode (extra blood)"), &g_Config.m_ClGores, 10.0f);
		SliderRow(&Right, &g_Config.m_ClGoresAmount, &g_Config.m_ClGoresAmount, Localize("Amount of gores"), 0, 100);

		CheckRow(&Right, &g_Config.m_ClBlockHelper, Localize("Block helper (highlight blocks)"), &g_Config.m_ClBlockHelper, 10.0f);
		SliderRow(&Right, &g_Config.m_ClBlockHelperAlpha, &g_Config.m_ClBlockHelperAlpha, Localize("Block helper opacity"), 0, 100);

		CheckRow(&Right, &g_Config.m_ClCleanScreen, Localize("Clean screen (hide HUD)"), &g_Config.m_ClCleanScreen, 10.0f);

		CheckRow(&Right, &g_Config.m_ClCustomHook, Localize("Custom hook color"), &g_Config.m_ClCustomHook, 10.0f);
		PickerRow(&Right, &s_HookColor, Localize("Hook color"), &g_Config.m_ClHookColor, ColorRGBA(1.0f, 1.0f, 1.0f), true, &g_Config.m_ClCustomHook, 0.0f);

		CheckRow(&Right, &g_Config.m_ClHookEffects, Localize("Hook glow effects"), &g_Config.m_ClHookEffects);
		SliderRow(&Right, &g_Config.m_ClHookGlowSize, &g_Config.m_ClHookGlowSize, Localize("Hook glow size (%)"), 100, 200);

		CheckRow(&Right, &g_Config.m_ClDragAim, Localize("Drag aim assist (lock nearest player)"), &g_Config.m_ClDragAim, 10.0f);
		CheckRow(&Right, &g_Config.m_ClDragAimLine, Localize("Drag aim target line"), &g_Config.m_ClDragAimLine);

		CheckRow(&Right, &g_Config.m_ClLocalServerPublic, Localize("Public local server (register online)"), &g_Config.m_ClLocalServerPublic, 10.0f);
	}

	s_ScrollRegion.End();
}
