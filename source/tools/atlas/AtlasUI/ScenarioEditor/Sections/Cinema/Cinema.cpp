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

#include "Cinema.h"

#include "tools/atlas/AtlasUI/ScenarioEditor/ScenarioEditor.h"
#include "tools/atlas/AtlasUI/ScenarioEditor/Sections/Common/Sidebar.h"
#include "tools/atlas/AtlasUI/ScenarioEditor/StyleSheet.h"
#include "tools/atlas/AtlasUI/ScenarioEditor/Tools/Common/Tools.h"
#include "tools/atlas/GameInterface/Messages.h"
#include "tools/atlas/GameInterface/Shareable.h"
#include "tools/atlas/GameInterface/SharedTypes.h"

#include <cstddef>
#include <string>
#include <vector>
#include <wx/button.h>
#include <wx/chartype.h>
#include <wx/checkbox.h>
#include <wx/listbox.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/string.h>
#include <wx/textctrl.h>
#include <wx/toolbar.h>
#include <wx/translation.h>
#include <wx/window.h>

using AtlasMessage::Shareable;

CinemaSidebar::CinemaSidebar(ScenarioEditor& scenarioEditor, wxWindow* sidebarContainer, wxWindow* bottomBarContainer)
	: Sidebar(scenarioEditor, sidebarContainer, bottomBarContainer)
{
	{
		auto* sizer = new wxStaticBoxSizer(wxVERTICAL, this, _T("Common settings"));

		m_DrawPath = new wxCheckBox(sizer->GetStaticBox(), wxID_ANY, _("Draw all paths"));
		m_DrawPath->SetToolTip(_("Display every cinematic path added to the map"));
		m_DrawPath->Bind(wxEVT_CHECKBOX, [this](auto&){ SetPathsDrawing(m_DrawPath->IsChecked()); });

		sizer->Add(m_DrawPath, wxSizerFlags().Expand().Border(wxALL, Atlas::Style::STATICBOX_PADDING));

		m_MainSizer->Add(sizer, wxSizerFlags().Expand());
	}

	{
		auto* boxSizer = new wxStaticBoxSizer(wxVERTICAL, this, _T("Paths"));
		auto* box = boxSizer->GetStaticBox();

		m_PathList = new wxListBox(box, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, NULL, wxLB_SINGLE | wxLB_SORT);

		auto* deleteButton = new wxButton(box, wxID_ANY, _("Delete"));
		deleteButton->SetToolTip(_T("Delete selected path"));
		deleteButton->Bind(wxEVT_BUTTON, [this](auto&){ DeleteSelectedPath(); });

		auto* newPathName = new wxTextCtrl(box, wxID_ANY);

		auto* addButton = new wxButton(box, wxID_ANY, _("Add"));
		addButton->Bind(wxEVT_BUTTON, [this, newPathName](auto&){ AddPath(newPathName->GetValue()); newPathName->Clear(); });

		wxFlexGridSizer* pathsSizer = new wxFlexGridSizer(1, 5, 5);
		pathsSizer->AddGrowableCol(0);
		pathsSizer->Add(m_PathList, wxSizerFlags().Proportion(1).Expand());
		pathsSizer->Add(deleteButton, wxSizerFlags().Expand());
		pathsSizer->Add(newPathName, wxSizerFlags().Expand());
		pathsSizer->Add(addButton, wxSizerFlags().Expand());

		boxSizer->Add(pathsSizer, wxSizerFlags().Expand().Border(wxALL, Atlas::Style::STATICBOX_PADDING));

		m_MainSizer->Add(boxSizer, wxSizerFlags().Expand());
	}
}

void CinemaSidebar::OnFirstDisplay()
{
	m_DrawPath->SetValue(false);

	ReloadPathList();
}

void CinemaSidebar::OnMapReload()
{
	m_DrawPath->SetValue(false);

	ReloadPathList();
}

void CinemaSidebar::SetPathsDrawing(const bool enable)
{
	POST_COMMAND(SetCinemaPathsDrawing, (enable));
}

void CinemaSidebar::AddPath(wxString name)
{
	if (name.empty())
		return;

	POST_COMMAND(AddCinemaPath, (name.ToStdWstring()));
	ReloadPathList();
}

void CinemaSidebar::DeleteSelectedPath()
{
	int index = m_PathList->GetSelection();
	if (index < 0)
		return;

	wxString pathName = m_PathList->GetString(index);
	if (pathName.empty())
		return;

	POST_COMMAND(DeleteCinemaPath, (pathName.ToStdWstring()));
	ReloadPathList();
}

void CinemaSidebar::ReloadPathList()
{
	const wxString selection = m_PathList->GetStringSelection();

	AtlasMessage::qGetCinemaPaths query_paths;
	query_paths.Post();

	m_PathList->Clear();
	for (const AtlasMessage::sCinemaPath& path : *query_paths.paths)
		m_PathList->Append(*path.name);

	m_PathList->SetStringSelection(selection);
}
