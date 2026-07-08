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

#include "Brushes.h"

#include "tools/atlas/GameInterface/MessagePasser.h"
#include "tools/atlas/GameInterface/Messages.h"

#include <cmath>
#include <cstddef>
#include <wx/arrstr.h>
#include <wx/chartype.h>
#include <wx/debug.h>
#include <wx/event.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/spinbutt.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/toolbar.h>
#include <wx/translation.h>

Brush g_Brush_Elevation; // shared between several elevation-related tools; other tools have their own brushes

static Brush* g_Brush_CurrentlyActive = NULL; // only one brush can be active at once

const float Brush::STRENGTH_MULTIPLIER = 1024.f;

Brush::Brush()
: m_Shape(Shape::CIRCLE), m_Size(4), m_Strength(1.f), m_IsActive(false)
{
}

Brush::~Brush()
{
	// Avoid dangling pointers
	if (g_Brush_CurrentlyActive == this)
		g_Brush_CurrentlyActive = NULL;
}

void Brush::MakeActive()
{
	if (g_Brush_CurrentlyActive)
		g_Brush_CurrentlyActive->m_IsActive = false;

	g_Brush_CurrentlyActive = this;
	m_IsActive = true;

	Send();
}

void Brush::Send()
{
	if (m_IsActive)
		POST_MESSAGE(Brush, (GetWidth(), GetHeight(), GetData()));
}

int Brush::GetWidth() const
{
	switch (m_Shape)
	{
	case Shape::CIRCLE:
		return m_Size;
	case Shape::SQUARE:
		return m_Size;
	default:
		wxFAIL;
		return -1;
	}
}

int Brush::GetHeight() const
{
/*
	switch (m_Shape)
	{
	case RECTANGLE or something:
	default:
		return GetWidth();
	}
*/
	return GetWidth();
}

std::vector<float> Brush::GetData() const
{
	int width = GetWidth();
	int height = GetHeight();

	std::vector<float> data (width*height);

	switch (m_Shape)
	{
	case Shape::CIRCLE:
		{
			int i = 0;
			// All calculations are done in units of half-tiles, since that
			// is the required precision
			int mid_x = m_Size-1;
			int mid_y = m_Size-1;
			for (int y = 0; y < m_Size; ++y)
			{
				for (int x = 0; x < m_Size; ++x)
				{
					float dist_sq = // scaled to 0 in centre, 1 on edge
						((2*x - mid_x)*(2*x - mid_x) +
						 (2*y - mid_y)*(2*y - mid_y)) / (float)(m_Size*m_Size);
					if (dist_sq <= 1.f)
						data[i++] = (sqrtf(2.f - dist_sq) - 1.f) / (sqrt(2.f) - 1.f);
					else
						data[i++] = 0.f;
				}
			}
			break;
		}

	case Shape::SQUARE:
		{
			int i = 0;
			for (int y = 0; y < height; ++y)
				for (int x = 0; x < width; ++x)
					data[i++] = 1.f;
			break;
		}
	}

	return data;
}

int Brush::GetSize() const
{
	return m_Size;
}

void Brush::SetSize(int size)
{
	m_Size = size;
	Send();
}

float Brush::GetStrength() const
{
	return m_Strength;
}

void Brush::SetStrength(float strength)
{
	m_Strength = strength;
	Send();
}

void Brush::SetShape(Shape shape)
{
	m_Shape = shape;
	Send();
}

void Brush::CreateUI(wxWindow* parent, wxSizer* sizer)
{
	// Must match order of Brush::Shape enum
	wxArrayString shapes;
	shapes.Add(_("Circle"));
	shapes.Add(_("Square"));

	// TODO (maybe): get rid of the extra static box, by not using wxRadioBox
	auto* brushShapeCtrl = new wxRadioBox(parent, wxID_ANY, _("Shape"), wxDefaultPosition, wxDefaultSize, shapes, 0, wxRA_SPECIFY_ROWS);
	brushShapeCtrl->Bind(wxEVT_RADIOBOX, [this, brushShapeCtrl](auto&){ SetShape(static_cast<Shape>(brushShapeCtrl->GetSelection())); });
	sizer->Add(brushShapeCtrl, wxSizerFlags().Expand().Border(wxALL, 5));

	sizer->AddSpacer(5);

	auto* brushSizeLabel = new wxStaticText(parent, wxID_ANY, _("Size"));
	auto* brushSizeCtrl = new wxSpinCtrl(parent, wxID_ANY,
		wxString::Format(_T("%d"), GetSize()),
		wxDefaultPosition, wxDefaultSize,
		wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER,
		0, 100, GetSize());
	auto burshSizeSetter = [this, brushSizeCtrl](auto&){ SetSize(brushSizeCtrl->GetValue()); };
	brushSizeCtrl->Bind(wxEVT_SPINCTRL, burshSizeSetter);

	auto* brushStrengthLabel =  new wxStaticText(parent, wxID_ANY, _("Strength"));
	auto* brushStrengthCtrl = new wxSpinCtrl(parent, wxID_ANY,
		wxString::Format(_T("%d"), static_cast<int>(10.f * GetStrength())),
		wxDefaultPosition, wxDefaultSize,
		wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER,
		0, 100, static_cast<int>(10.f * GetStrength()));
	auto burshStrenghtSetter = [this, brushStrengthCtrl](auto&){ SetStrength(brushStrengthCtrl->GetValue() / 10.f); };
	brushStrengthCtrl->Bind(wxEVT_SPINCTRL, burshStrenghtSetter);

	wxFlexGridSizer* spinnerSizer = new wxFlexGridSizer(2, 5, 5);
	spinnerSizer->AddGrowableCol(1);

	spinnerSizer->Add(brushSizeLabel, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT));
	spinnerSizer->Add(brushSizeCtrl, wxSizerFlags().Expand());
	spinnerSizer->Add(brushStrengthLabel, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL|wxALIGN_RIGHT));
	spinnerSizer->Add(brushStrengthCtrl, wxSizerFlags().Expand());

	sizer->Add(spinnerSizer, wxSizerFlags().Expand().Border(wxALL, 5));
}
