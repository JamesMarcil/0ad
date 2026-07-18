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

#include "tools/atlas/AtlasUI/ScenarioEditor/Sections/Common/Sidebar.h"

#include <memory>
#include <wx/event.h>

class ITool;
class ScenarioEditor;
class wxWindow;
struct ObjectSidebarImpl;

class ObjectSidebar : public Sidebar
{
public:
	ObjectSidebar(ScenarioEditor& scenarioEditor, wxWindow* sidebarContainer, wxWindow* bottomBarContainer);

	void FilterObjects();

protected:
	void OnFirstDisplay() override;

private:
	void OnToolChange(ITool* tool);
	void OnToggleViewer(wxCommandEvent& evt);
	void OnSelectType(wxCommandEvent& evt);
	void OnSelectFilter(wxCommandEvent& evt);
	void OnSelectObject(wxCommandEvent& evt);
	void OnToggleExactFilter(wxCommandEvent& evt);

	std::unique_ptr<ObjectSidebarImpl> m_Impl;

	wxDECLARE_EVENT_TABLE();
};
