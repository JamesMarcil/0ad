/* Copyright (C) 2026 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "precompiled.h"

#include "MessageHandler.h"
#include "gui/CGUI.h"
#include "gui/GUIManager.h"
#include "lib/external_libraries/libsdl.h"
#include "lib/path.h"
#include "lib/types.h"
#include "maths/MathUtil.h"
#include "ps/Game.h"
#include "ps/GameSetup/Config.h"
#include "ps/GameSetup/GameSetup.h"
#include "ps/Input.h"
#include "ps/VideoMode.h"
#include "renderer/Renderer.h"
#include "scriptinterface/Interface.h"
#include "simulation2/components/ICmpSoundManager.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Entity.h"
#include "tools/atlas/GameInterface/MessagePasser.h"
#include "tools/atlas/GameInterface/MessagePasserImpl.h"
#include "tools/atlas/GameInterface/Messages.h"
#include "tools/atlas/GameInterface/Shareable.h"
#include "tools/atlas/GameInterface/SharedTypes.h"
#include "tools/atlas/GameInterface/View.h"

#include <SDL_events.h>
#include <SDL_keyboard.h>
#include <SDL_keycode.h>
#include <cstddef>
#include <js/CallArgs.h>
#include <js/RootingAPI.h>
#include <memory>
#include <string>

extern void (*Atlas_GLSwapBuffers)(void* context);

namespace AtlasMessage
{

MESSAGEHANDLER(MessageTrace)
{
	((MessagePasserImpl*)g_MessagePasser)->SetTrace(msg->enable);
}

MESSAGEHANDLER(Screenshot)
{
	if (msg->big)
		g_Renderer.MakeScreenShotOnNextFrame(CRenderer::ScreenShotType::BIG);
	else
		g_Renderer.MakeScreenShotOnNextFrame(CRenderer::ScreenShotType::DEFAULT);
}

QUERYHANDLER(Ping)
{
}

MESSAGEHANDLER(SimStopMusic)
{
	CmpPtr<ICmpSoundManager> cmpSoundManager(*g_Game->GetSimulation2(), SYSTEM_ENTITY);
	if (cmpSoundManager)
		cmpSoundManager->StopMusic();
}

MESSAGEHANDLER(SimStateSave)
{
	AtlasView::GetView_Game()->SaveState(*msg->label);
}

MESSAGEHANDLER(SimStateRestore)
{
	AtlasView::GetView_Game()->RestoreState(*msg->label);
}

QUERYHANDLER(SimStateDebugDump)
{
	msg->dump = AtlasView::GetView_Game()->DumpState(msg->binary);
}

MESSAGEHANDLER(SimPlay)
{
	AtlasView::GetView_Game()->SetSpeedMultiplier(msg->speed);
	AtlasView::GetView_Game()->SetTesting(msg->simTest);
}

MESSAGEHANDLER(SetSmoothFramerate)
{
	AtlasView::GetView_Game()->SetSmoothFramerate(msg->enabled);
	AtlasView::GetView_Actor()->SetSmoothFramerate(msg->enabled);
}

MESSAGEHANDLER(JavaScript)
{
	g_GUI->GetActiveGUI()->GetScriptInterface()->LoadGlobalScript(L"Atlas", *msg->command);
}

MESSAGEHANDLER(GuiSwitchPage)
{
	g_GUI->SwitchPage(*msg->page, NULL, JS::UndefinedHandleValue);
}

MESSAGEHANDLER(GuiMouseButtonEvent)
{
	if (!g_VideoMode.IsInitialized())
		return;

	SDL_Event ev{};
	ev.type = msg->pressed ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	ev.button.button = msg->button;
	ev.button.state = msg->pressed ? SDL_PRESSED : SDL_RELEASED;
	ev.button.clicks = msg->clicks;
	float x, y;
	msg->pos->GetScreenSpace(x, y);
	ev.button.x = static_cast<u16>(Clamp<int>(x, 0, g_VideoMode.GetWindowWidth()));
	ev.button.y = static_cast<u16>(Clamp<int>(y, 0, g_VideoMode.GetWindowHeight()));
	g_VideoMode.m_InputManager.DispatchEvent(ev);
}

MESSAGEHANDLER(GuiMouseMotionEvent)
{
	if (!g_VideoMode.IsInitialized())
		return;

	SDL_Event ev{};
	ev.type = SDL_MOUSEMOTION;
	float x, y;
	msg->pos->GetScreenSpace(x, y);
	ev.motion.x = static_cast<u16>(Clamp<int>(x, 0, g_VideoMode.GetWindowWidth()));
	ev.motion.y = static_cast<u16>(Clamp<int>(y, 0, g_VideoMode.GetWindowHeight()));
	g_VideoMode.m_InputManager.DispatchEvent(ev);
}

MESSAGEHANDLER(GuiKeyEvent)
{
	if (!g_VideoMode.IsInitialized())
		return;

	SDL_Event ev{};
	ev.type = msg->pressed ? SDL_KEYDOWN : SDL_KEYUP;
	ev.key.keysym.sym = static_cast<SDL_Keycode>(static_cast<int>(msg->sdlkey));
	ev.key.keysym.scancode = SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(static_cast<int>(msg->sdlkey)));
	g_VideoMode.m_InputManager.DispatchEvent(ev);
}

MESSAGEHANDLER(GuiCharEvent)
{
	if (!g_VideoMode.IsInitialized())
		return;

	// Simulate special 'text input' events in the SDL
	// This isn't quite compatible with WXWidget's handling,
	// so to avoid trouble we only send 'letter-like' ASCII input.
	SDL_Event ev{};
	ev.type = SDL_TEXTEDITING;
	ev.text.type = SDL_TEXTEDITING;
	ev.text.text[0] = static_cast<char>(msg->sdlkey);
	ev.text.text[1] = '\0';
	g_VideoMode.m_InputManager.DispatchEvent(ev);

	ev.type = SDL_TEXTINPUT;
	ev.text.type = SDL_TEXTINPUT;
	ev.text.text[0] = static_cast<char>(msg->sdlkey);
	ev.text.text[1] = '\0';
	g_VideoMode.m_InputManager.DispatchEvent(ev);
}

} // namespace AtlasMessage
