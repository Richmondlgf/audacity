/**********************************************************************

  Audacity: A Digital Audio Editor

  AttachableScrollBar.h

  James Crook
 
  A scroll bar that can be attached to multiple items and so control 
  their scrolling.

  Audacity is free software.
  This file is licensed under the wxWindows license, see License.txt

**********************************************************************/

#ifndef __AUDACITY_ATTACHABLE_SCROLL_BAR__
#define __AUDACITY_ATTACHABLE_SCROLL_BAR__

#include <wx/scrolbar.h>

struct ViewInfo;

class AttachableScrollBar :
	public wxScrollBar
{
public:
	AttachableScrollBar(
	wxWindow* parent, 
	wxWindowID id, 
	const wxPoint& pos = wxDefaultPosition, 
	const wxSize& size = wxDefaultSize, 
	long style = wxSB_HORIZONTAL);
public:
	~AttachableScrollBar(void);
	void OnScroll(wxScrollEvent & event);
	void SetViewInfo( ViewInfo * view );

   void SetScrollBarFromViewInfo();
   void SetViewInfoFromScrollBar();

	ViewInfo * mpViewInfo;
   DECLARE_EVENT_TABLE();
};

#endif // __AUDACITY_ATTACHABLE_SCROLL_BAR__

// Indentation settings for Vim and Emacs
// Please do not modify past this point.
//
// Local Variables:
// c-basic-offset: 3
// indent-tabs-mode: nil
// End:


