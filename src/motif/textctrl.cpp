/////////////////////////////////////////////////////////////////////////////
// Name:        textctrl.cpp
// Purpose:     wxTextCtrl
// Author:      Julian Smart
// Modified by:
// Created:     17/09/98
// RCS-ID:      $Id$
// Copyright:   (c) Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#ifdef __GNUG__
    #pragma implementation "textctrl.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fstream.h>
#include <ctype.h>

#include "wx/textctrl.h"
#include "wx/settings.h"
#include "wx/filefn.h"
#include "wx/utils.h"

#include <Xm/Text.h>

#include "wx/motif/private.h"

// ----------------------------------------------------------------------------
// private functions
// ----------------------------------------------------------------------------

// helper: inserts the new text in the value of the text ctrl and returns the
// result in place
static void MergeChangesIntoString(wxString& value,
                                   XmTextVerifyCallbackStruct *textStruct);

// callbacks
static void wxTextWindowChangedProc(Widget w, XtPointer clientData, XtPointer ptr);
static void wxTextWindowModifyProc(Widget w, XtPointer clientData, XmTextVerifyCallbackStruct *cbs);
static void wxTextWindowGainFocusProc(Widget w, XtPointer clientData, XmAnyCallbackStruct *cbs);
static void wxTextWindowLoseFocusProc(Widget w, XtPointer clientData, XmAnyCallbackStruct *cbs);
static void wxTextWindowActivateProc(Widget w, XtPointer clientData, XmAnyCallbackStruct *ptr);

#if !USE_SHARED_LIBRARY
    IMPLEMENT_DYNAMIC_CLASS(wxTextCtrl, wxControl)

    BEGIN_EVENT_TABLE(wxTextCtrl, wxControl)
        EVT_DROP_FILES(wxTextCtrl::OnDropFiles)
        EVT_CHAR(wxTextCtrl::OnChar)
    END_EVENT_TABLE()
#endif

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// wxTextCtrl
// ----------------------------------------------------------------------------

// Text item
wxTextCtrl::wxTextCtrl()
#ifndef NO_TEXT_WINDOW_STREAM
          : streambuf()
#endif
{
    m_tempCallbackStruct = (void*) NULL;
    m_modified = FALSE;
    m_processedDefault = FALSE;
}

bool wxTextCtrl::Create(wxWindow *parent,
                        wxWindowID id,
                        const wxString& value,
                        const wxPoint& pos,
                        const wxSize& size,
                        long style,
                        const wxValidator& validator,
                        const wxString& name)
{
    m_tempCallbackStruct = (void*) NULL;
    m_modified = FALSE;
    m_processedDefault = FALSE;
    //    m_backgroundColour = parent->GetBackgroundColour();
    m_backgroundColour = * wxWHITE;
    m_foregroundColour = parent->GetForegroundColour();

    SetName(name);
    SetValidator(validator);
    if (parent)
        parent->AddChild(this);

    m_windowStyle = style;

    if ( id == -1 )
        m_windowId = (int)NewControlId();
    else
        m_windowId = id;

    Widget parentWidget = (Widget) parent->GetClientWidget();

    bool wantHorizScrolling = ((m_windowStyle & wxHSCROLL) != 0);

    // If we don't have horizontal scrollbars, we want word wrap.
    bool wantWordWrap = !wantHorizScrolling;

    if (m_windowStyle & wxTE_MULTILINE)
    {
        Arg args[2];
        XtSetArg (args[0], XmNscrollHorizontal, wantHorizScrolling ? True : False);
        XtSetArg (args[1], XmNwordWrap, wantWordWrap ? True : False);

        m_mainWidget = (WXWidget) XmCreateScrolledText(parentWidget,
                                                       (char*)name.c_str(),
                                                       args, 2);

        XtVaSetValues ((Widget) m_mainWidget,
                        XmNeditable, ((style & wxTE_READONLY) ? False : True),
                        XmNeditMode, XmMULTI_LINE_EDIT,
                        NULL);
        XtManageChild ((Widget) m_mainWidget);
    }
    else
    {
        m_mainWidget = (WXWidget)XtVaCreateManagedWidget
                                 (
                                  (char*)name.c_str(),
                                  xmTextWidgetClass,
                                  parentWidget,
                                  NULL
                                 );

        // TODO: Is this relevant? What does it do?
        int noCols = 2;
        if (!value.IsNull() && (value.Length() > (unsigned int) noCols))
            noCols = value.Length();
        XtVaSetValues((Widget) m_mainWidget,
                      XmNcolumns, noCols,
                      NULL);
    }

    // remove border if asked for
    if ( style & wxNO_BORDER )
    {
        XtVaSetValues((Widget)m_mainWidget,
                      XmNshadowThickness, 0,
                      NULL);
    }

    if ( !!value )
        XmTextSetString ((Widget) m_mainWidget, (char*)value.c_str());

    // install callbacks
    XtAddCallback((Widget) m_mainWidget, XmNvalueChangedCallback, (XtCallbackProc)wxTextWindowChangedProc, (XtPointer)this);

    XtAddCallback((Widget) m_mainWidget, XmNmodifyVerifyCallback, (XtCallbackProc)wxTextWindowModifyProc, (XtPointer)this);

    XtAddCallback((Widget) m_mainWidget, XmNactivateCallback, (XtCallbackProc)wxTextWindowActivateProc, (XtPointer)this);

    XtAddCallback((Widget) m_mainWidget, XmNfocusCallback, (XtCallbackProc)wxTextWindowGainFocusProc, (XtPointer)this);

    XtAddCallback((Widget) m_mainWidget, XmNlosingFocusCallback, (XtCallbackProc)wxTextWindowLoseFocusProc, (XtPointer)this);

    // font
    m_windowFont = parent->GetFont();
    ChangeFont(FALSE);

    SetCanAddEventHandler(TRUE);
    AttachWidget (parent, m_mainWidget, (WXWidget) NULL, pos.x, pos.y, size.x, size.y);

    ChangeBackgroundColour();

    return TRUE;
}

WXWidget wxTextCtrl::GetTopWidget() const
{
    return ((m_windowStyle & wxTE_MULTILINE) ? (WXWidget) XtParent((Widget) m_mainWidget) : m_mainWidget);
}

wxString wxTextCtrl::GetValue() const
{
    wxString str; // result

    if (m_windowStyle & wxTE_PASSWORD)
    {
        // the value is stored always in m_value because it can't be retrieved
        // from the text control
        str = m_value;
    }
    else
    {
        // just get the string from Motif
        char *s = XmTextGetString ((Widget) m_mainWidget);
        if ( s )
        {
            str = s;
            XtFree (s);
        }
        //else: return empty string

        if ( m_tempCallbackStruct )
        {
            // the string in the control isn't yet updated, can't use it as is
            MergeChangesIntoString(str, (XmTextVerifyCallbackStruct *)
                                   m_tempCallbackStruct);
        }
    }

    return str;
}

void wxTextCtrl::SetValue(const wxString& value)
{
    m_inSetValue = TRUE;

    XmTextSetString ((Widget) m_mainWidget, (char*)value.c_str());

    m_inSetValue = FALSE;
}

// Clipboard operations
void wxTextCtrl::Copy()
{
    XmTextCopy((Widget) m_mainWidget, CurrentTime);
}

void wxTextCtrl::Cut()
{
    XmTextCut((Widget) m_mainWidget, CurrentTime);
}

void wxTextCtrl::Paste()
{
    XmTextPaste((Widget) m_mainWidget);
}

bool wxTextCtrl::CanCopy() const
{
    // Can copy if there's a selection
    long from, to;
    GetSelection(& from, & to);
    return (from != to) ;
}

bool wxTextCtrl::CanCut() const
{
    // Can cut if there's a selection
    long from, to;
    GetSelection(& from, & to);
    return (from != to) ;
}

bool wxTextCtrl::CanPaste() const
{
    return IsEditable() ;
}

// Undo/redo
void wxTextCtrl::Undo()
{
    // Not possible in Motif
}

void wxTextCtrl::Redo()
{
    // Not possible in Motif
}

bool wxTextCtrl::CanUndo() const
{
    // No Undo in Motif
    return FALSE;
}

bool wxTextCtrl::CanRedo() const
{
    // No Redo in Motif
    return FALSE;
}

// If the return values from and to are the same, there is no
// selection.
void wxTextCtrl::GetSelection(long* from, long* to) const
{
    XmTextPosition left, right;

    XmTextGetSelectionPosition((Widget) m_mainWidget, & left, & right);

    *from = (long) left;
    *to = (long) right;
}

bool wxTextCtrl::IsEditable() const
{
    return (XmTextGetEditable((Widget) m_mainWidget) != 0);
}

void wxTextCtrl::SetEditable(bool editable)
{
    XmTextSetEditable((Widget) m_mainWidget, (Boolean) editable);
}

void wxTextCtrl::SetInsertionPoint(long pos)
{
    XmTextSetInsertionPosition ((Widget) m_mainWidget, (XmTextPosition) pos);
}

void wxTextCtrl::SetInsertionPointEnd()
{
    long pos = GetLastPosition();
    SetInsertionPoint(pos);
}

long wxTextCtrl::GetInsertionPoint() const
{
    return (long) XmTextGetInsertionPosition ((Widget) m_mainWidget);
}

long wxTextCtrl::GetLastPosition() const
{
    return (long) XmTextGetLastPosition ((Widget) m_mainWidget);
}

void wxTextCtrl::Replace(long from, long to, const wxString& value)
{
    XmTextReplace ((Widget) m_mainWidget, (XmTextPosition) from, (XmTextPosition) to,
        (char*) (const char*) value);
}

void wxTextCtrl::Remove(long from, long to)
{
    XmTextSetSelection ((Widget) m_mainWidget, (XmTextPosition) from, (XmTextPosition) to,
                      (Time) 0);
    XmTextRemove ((Widget) m_mainWidget);
}

void wxTextCtrl::SetSelection(long from, long to)
{
    XmTextSetSelection ((Widget) m_mainWidget, (XmTextPosition) from, (XmTextPosition) to,
                      (Time) 0);
}

bool wxTextCtrl::LoadFile(const wxString& file)
{
    if (!wxFileExists(file))
        return FALSE;

    m_fileName = file;

    Clear();

    Widget textWidget = (Widget) m_mainWidget;
    FILE *fp;

    struct stat statb;
    if ((stat ((char*) (const char*) file, &statb) == -1) || (statb.st_mode & S_IFMT) != S_IFREG ||
        !(fp = fopen ((char*) (const char*) file, "r")))
    {
        return FALSE;
    }
    else
    {
        long len = statb.st_size;
        char *text;
        if (!(text = XtMalloc ((unsigned) (len + 1))))
        {
            fclose (fp);
            return FALSE;
        }
        if (fread (text, sizeof (char), len, fp) != (size_t) len)
        {
        }
        fclose (fp);

        text[len] = 0;
        XmTextSetString (textWidget, text);
        //      m_textPosition = len;
        XtFree (text);
        m_modified = FALSE;
        return TRUE;
    }
}

// If file is null, try saved file name first
// Returns TRUE if succeeds.
bool wxTextCtrl::SaveFile(const wxString& file)
{
    wxString theFile(file);
    if (theFile == "")
        theFile = m_fileName;
    if (theFile == "")
        return FALSE;
    m_fileName = theFile;

    Widget textWidget = (Widget) m_mainWidget;
    FILE *fp;

    if (!(fp = fopen ((char*) (const char*) theFile, "w")))
    {
        return FALSE;
    }
    else
    {
        char *text = XmTextGetString (textWidget);
        long len = XmTextGetLastPosition (textWidget);

        if (fwrite (text, sizeof (char), len, fp) != (size_t) len)
        {
            // Did not write whole file
        }
        // Make sure newline terminates the file
        if (text[len - 1] != '\n')
            fputc ('\n', fp);

        fclose (fp);
        XtFree (text);
        m_modified = FALSE;
        return TRUE;
    }
}

void wxTextCtrl::WriteText(const wxString& text)
{
    long textPosition = GetInsertionPoint() + strlen (text);
    XmTextInsert ((Widget) m_mainWidget, GetInsertionPoint(), (char*) (const char*) text);
    XtVaSetValues ((Widget) m_mainWidget, XmNcursorPosition, textPosition, NULL);
    SetInsertionPoint(textPosition);
    XmTextShowPosition ((Widget) m_mainWidget, textPosition);
    m_modified = TRUE;
}

void wxTextCtrl::AppendText(const wxString& text)
{
    long textPosition = GetLastPosition() + strlen(text);
    XmTextInsert ((Widget) m_mainWidget, GetLastPosition(), (char*) (const char*) text);
    XtVaSetValues ((Widget) m_mainWidget, XmNcursorPosition, textPosition, NULL);
    SetInsertionPoint(textPosition);
    XmTextShowPosition ((Widget) m_mainWidget, textPosition);
    m_modified = TRUE;
}

void wxTextCtrl::Clear()
{
    XmTextSetString ((Widget) m_mainWidget, "");
    m_modified = FALSE;
}

bool wxTextCtrl::IsModified() const
{
    return m_modified;
}

// Makes 'unmodified'
void wxTextCtrl::DiscardEdits()
{
    XmTextSetString ((Widget) m_mainWidget, "");
    m_modified = FALSE;
}

int wxTextCtrl::GetNumberOfLines() const
{
    // HIDEOUSLY inefficient, but we have no choice.
    char *s = XmTextGetString ((Widget) m_mainWidget);
    if (s)
    {
        long i = 0;
        int currentLine = 0;
        bool finished = FALSE;
        while (!finished)
        {
            int ch = s[i];
            if (ch == '\n')
            {
                currentLine++;
                i++;
            }
            else if (ch == 0)
            {
                finished = TRUE;
            }
            else
                i++;
        }

        XtFree (s);
        return currentLine;
    }
    return 0;
}

long wxTextCtrl::XYToPosition(long x, long y) const
{
/* It seems, that there is a bug in some versions of the Motif library,
    so the original wxWin-Code doesn't work. */
    /*
    Widget textWidget = (Widget) handle;
    return (long) XmTextXYToPos (textWidget, (Position) x, (Position) y);
    */
    /* Now a little workaround: */
    long r=0;
    for (int i=0; i<y; i++) r+=(GetLineLength(i)+1);
    return r+x;
}

void wxTextCtrl::PositionToXY(long pos, long *x, long *y) const
{
    Position xx, yy;
    XmTextPosToXY((Widget) m_mainWidget, pos, &xx, &yy);
    *x = xx; *y = yy;
}

void wxTextCtrl::ShowPosition(long pos)
{
    XmTextShowPosition ((Widget) m_mainWidget, (XmTextPosition) pos);
}

int wxTextCtrl::GetLineLength(long lineNo) const
{
    wxString str = GetLineText (lineNo);
    return (int) str.Length();
}

wxString wxTextCtrl::GetLineText(long lineNo) const
{
    // HIDEOUSLY inefficient, but we have no choice.
    char *s = XmTextGetString ((Widget) m_mainWidget);

    if (s)
    {
        wxString buf("");
        long i;
        int currentLine = 0;
        for (i = 0; currentLine != lineNo && s[i]; i++ )
            if (s[i] == '\n')
                currentLine++;
            // Now get the text
            int j;
            for (j = 0; s[i] && s[i] != '\n'; i++, j++ )
                buf += s[i];

            XtFree(s);
            return buf;
    }
    else
        return wxEmptyString;
}

/*
* Text item
*/

void wxTextCtrl::Command(wxCommandEvent & event)
{
    SetValue (event.GetString());
    ProcessCommand (event);
}

void wxTextCtrl::OnDropFiles(wxDropFilesEvent& event)
{
    // By default, load the first file into the text window.
    if (event.GetNumberOfFiles() > 0)
    {
        LoadFile(event.GetFiles()[0]);
    }
}

// The streambuf code was partly taken from chapter 3 by Jerry Schwarz of
// AT&T's "C++ Lanuage System Release 3.0 Library Manual" - Stein Somers

//=========================================================================
// Called then the buffer is full (gcc 2.6.3)
// or when "endl" is output (Borland 4.5)
//=========================================================================
// Class declaration using multiple inheritance doesn't work properly for
// Borland. See note in wb_text.h.
#ifndef NO_TEXT_WINDOW_STREAM
int wxTextCtrl::overflow(int c)
{
    // Make sure there is a holding area
    if ( allocate()==EOF )
    {
        wxError("Streambuf allocation failed","Internal error");
        return EOF;
    }

    // Verify that there are no characters in get area
    if ( gptr() && gptr() < egptr() )
    {
        wxError("wxTextCtrl::overflow: Who's trespassing my get area?","Internal error");
        return EOF;
    }

    // Reset get area
    setg(0,0,0);

    // Make sure there is a put area
    if ( ! pptr() )
    {
        /* This doesn't seem to be fatal so comment out error message */
        //    wxError("Put area not opened","Internal error");
        setp( base(), base() );
    }

    // Determine how many characters have been inserted but no consumed
    int plen = pptr() - pbase();

    // Now Jerry relies on the fact that the buffer is at least 2 chars
    // long, but the holding area "may be as small as 1" ???
    // And we need an additional \0, so let's keep this inefficient but
    // safe copy.

    // If c!=EOF, it is a character that must also be comsumed
    int xtra = c==EOF? 0 : 1;

    // Write temporary C-string to wxTextWindow
    {
        char *txt = new char[plen+xtra+1];
        memcpy(txt, pbase(), plen);
        txt[plen] = (char)c;     // append c
        txt[plen+xtra] = '\0';   // append '\0' or overwrite c
        // If the put area already contained \0, output will be truncated there
        WriteText(txt);
        delete[] txt;
    }

    // Reset put area
    setp(pbase(), epptr());

#if defined(__WATCOMC__)
    return __NOT_EOF;
#elif defined(zapeof)     // HP-UX (all cfront based?)
    return zapeof(c);
#else
    return c!=EOF ? c : 0;  // this should make everybody happy
#endif
}

//=========================================================================
// called then "endl" is output (gcc) or then explicit sync is done (Borland)
//=========================================================================
int wxTextCtrl::sync()
{
    // Verify that there are no characters in get area
    if ( gptr() && gptr() < egptr() )
    {
        wxError("Who's trespassing my get area?","Internal error");
        return EOF;
    }

    if ( pptr() && pptr() > pbase() ) return overflow(EOF);

    return 0;
    /* OLD CODE
    int len = pptr() - pbase();
    char *txt = new char[len+1];
    strncpy(txt, pbase(), len);
    txt[len] = '\0';
    (*this) << txt;
    setp(pbase(), epptr());
    delete[] txt;
    return 0;
    */
}

//=========================================================================
// Should not be called by a "ostream". Used by a "istream"
//=========================================================================
int wxTextCtrl::underflow()
{
    return EOF;
}
#endif

wxTextCtrl& wxTextCtrl::operator<<(const wxString& s)
{
    AppendText(s);
    return *this;
}

wxTextCtrl& wxTextCtrl::operator<<(float f)
{
    wxString str;
    str.Printf("%.2f", f);
    AppendText(str);
    return *this;
}

wxTextCtrl& wxTextCtrl::operator<<(double d)
{
    wxString str;
    str.Printf("%.2f", d);
    AppendText(str);
    return *this;
}

wxTextCtrl& wxTextCtrl::operator<<(int i)
{
    wxString str;
    str.Printf("%d", i);
    AppendText(str);
    return *this;
}

wxTextCtrl& wxTextCtrl::operator<<(long i)
{
    wxString str;
    str.Printf("%ld", i);
    AppendText(str);
    return *this;
}

wxTextCtrl& wxTextCtrl::operator<<(const char c)
{
    char buf[2];

    buf[0] = c;
    buf[1] = 0;
    AppendText(buf);
    return *this;
}

void wxTextCtrl::OnChar(wxKeyEvent& event)
{
    // Indicates that we should generate a normal command, because
    // we're letting default behaviour happen (otherwise it's vetoed
    // by virtue of overriding OnChar)
    m_processedDefault = TRUE;

    if (m_tempCallbackStruct)
    {
        XmTextVerifyCallbackStruct *textStruct =
            (XmTextVerifyCallbackStruct *) m_tempCallbackStruct;
        textStruct->doit = True;
        if (isascii(event.m_keyCode) && (textStruct->text->length == 1))
        {
            textStruct->text->ptr[0] = ((event.m_keyCode == WXK_RETURN) ? 10 : event.m_keyCode);
        }
    }
}

void wxTextCtrl::ChangeFont(bool keepOriginalSize)
{
    wxWindow::ChangeFont(keepOriginalSize);
}

void wxTextCtrl::ChangeBackgroundColour()
{
    wxWindow::ChangeBackgroundColour();

    /* TODO: should scrollbars be affected? Should probably have separate
    * function to change them (by default, taken from wxSystemSettings)
    */
    if (m_windowStyle & wxTE_MULTILINE)
    {
        Widget parent = XtParent ((Widget) m_mainWidget);
        Widget hsb, vsb;

        XtVaGetValues (parent,
            XmNhorizontalScrollBar, &hsb,
            XmNverticalScrollBar, &vsb,
            NULL);
        wxColour backgroundColour = wxSystemSettings::GetSystemColour(wxSYS_COLOUR_3DFACE);
        if (hsb)
            DoChangeBackgroundColour((WXWidget) hsb, backgroundColour, TRUE);
        if (vsb)
            DoChangeBackgroundColour((WXWidget) vsb, backgroundColour, TRUE);

        DoChangeBackgroundColour((WXWidget) parent, m_backgroundColour, TRUE);
    }
}

void wxTextCtrl::ChangeForegroundColour()
{
    wxWindow::ChangeForegroundColour();

    if (m_windowStyle & wxTE_MULTILINE)
    {
        Widget parent = XtParent ((Widget) m_mainWidget);
        Widget hsb, vsb;

        XtVaGetValues (parent,
            XmNhorizontalScrollBar, &hsb,
            XmNverticalScrollBar, &vsb,
            NULL);

            /* TODO: should scrollbars be affected? Should probably have separate
            * function to change them (by default, taken from wxSystemSettings)
            if (hsb)
            DoChangeForegroundColour((WXWidget) hsb, m_foregroundColour);
            if (vsb)
            DoChangeForegroundColour((WXWidget) vsb, m_foregroundColour);
        */
        DoChangeForegroundColour((WXWidget) parent, m_foregroundColour);
    }
}

void wxTextCtrl::DoSendEvents(void *wxcbs, long keycode)
{
    // we're in process of updating the text control
    m_tempCallbackStruct = wxcbs;

    XmTextVerifyCallbackStruct *cbs = (XmTextVerifyCallbackStruct *)wxcbs;

    wxKeyEvent event (wxEVT_CHAR);
    event.SetId(GetId());
    event.m_keyCode = keycode;
    event.SetEventObject(this);

    // Only if wxTextCtrl::OnChar is called will this be set to True (and
    // the character passed through)
    cbs->doit = False;

    GetEventHandler()->ProcessEvent(event);

    if ( !InSetValue() && m_processedDefault )
    {
        // Can generate a command
        wxCommandEvent commandEvent(wxEVT_COMMAND_TEXT_UPDATED, GetId());
        commandEvent.SetEventObject(this);
        ProcessCommand(commandEvent);
    }

    // do it after the (user) event handlers processed the events because
    // otherwise GetValue() would return incorrect (not yet updated value)
    m_tempCallbackStruct = NULL;
}

// ----------------------------------------------------------------------------
// helpers and Motif callbacks
// ----------------------------------------------------------------------------

static void MergeChangesIntoString(wxString& value,
                                   XmTextVerifyCallbackStruct *cbs)
{
    /* _sm_
     * At least on my system (SunOS 4.1.3 + Motif 1.2), you need to think of
     * every event as a replace event.  cbs->text->ptr gives the replacement
     * text, cbs->startPos gives the index of the first char affected by the
     * replace, and cbs->endPos gives the index one more than the last char
     * affected by the replace (startPos == endPos implies an empty range).
     * Hence, a deletion is represented by replacing all input text with a
     * blank string ("", *not* NULL!).  A simple insertion that does not
     * overwrite any text has startPos == endPos.
     */

    if ( !value )
    {
        // easy case: the ol value was empty
        value = cbs->text->ptr;
    }
    else
    {
        // merge the changes into the value
        const char * const passwd = value;
        int len = value.length();

        len += strlen(cbs->text->ptr) + 1;     // + new text (if any) + NUL
        len -= cbs->endPos - cbs->startPos;    // - text from affected region.

        char * newS = new char [len];
        char * dest = newS,
             * insert = cbs->text->ptr;

        // Copy (old) text from passwd, up to the start posn of the change.
        int i;
        const char * p = passwd;
        for (i = 0; i < cbs->startPos; ++i)
            *dest++ = *p++;

        // Copy the text to be inserted).
        while (*insert)
            *dest++ = *insert++;

        // Finally, copy into newS any remaining text from passwd[endPos] on.
        for (p = passwd + cbs->endPos; *p; )
            *dest++ = *p++;
        *dest = 0;

        value = newS;

        delete[] newS;
    }
}

static void
wxTextWindowChangedProc (Widget w, XtPointer clientData, XtPointer ptr)
{
    if (!wxGetWindowFromTable(w))
        // Widget has been deleted!
        return;

    wxTextCtrl *tw = (wxTextCtrl *) clientData;
    tw->SetModified(TRUE);
}

static void
wxTextWindowModifyProc (Widget w, XtPointer clientData, XmTextVerifyCallbackStruct *cbs)
{
    wxTextCtrl *tw = (wxTextCtrl *) clientData;
    tw->m_processedDefault = FALSE;

    // First, do some stuff if it's a password control: in this case, we need
    // to store the string inside the class because GetValue() can't retrieve
    // it from the text ctrl. We do *not* do it in other circumstances because
    // it would double the amount of memory needed.

    if ( tw->GetWindowStyleFlag() & wxTE_PASSWORD )
    {
        MergeChangesIntoString(tw->m_value, cbs);

        if ( cbs->text->length > 0 )
        {
            int i;
            for (i = 0; i < cbs->text->length; ++i)
                cbs->text->ptr[i] = '*';
            cbs->text->ptr[i] = '\0';
        }
    }

    // If we're already within an OnChar, return: probably a programmatic
    // insertion.
    if (tw->m_tempCallbackStruct)
        return;

    // Check for a backspace
    if (cbs->startPos == (cbs->currInsert - 1))
    {
        tw->DoSendEvents((void *)cbs, WXK_DELETE);

        return;
    }

    // Pasting operation: let it through without calling OnChar
    if (cbs->text->length > 1)
        return;

    // Something other than text
    if (cbs->text->ptr == NULL)
        return;

    // normal key press
    char ch = cbs->text->ptr[0];
    tw->DoSendEvents((void *)cbs, ch == '\n' ? '\r' : ch);
}

static void
wxTextWindowGainFocusProc (Widget w, XtPointer clientData, XmAnyCallbackStruct *cbs)
{
    if (!wxGetWindowFromTable(w))
        return;

    wxTextCtrl *tw = (wxTextCtrl *) clientData;
    wxFocusEvent event(wxEVT_SET_FOCUS, tw->GetId());
    event.SetEventObject(tw);
    tw->GetEventHandler()->ProcessEvent(event);
}

static void
wxTextWindowLoseFocusProc (Widget w, XtPointer clientData, XmAnyCallbackStruct *cbs)
{
    if (!wxGetWindowFromTable(w))
        return;

    wxTextCtrl *tw = (wxTextCtrl *) clientData;
    wxFocusEvent event(wxEVT_KILL_FOCUS, tw->GetId());
    event.SetEventObject(tw);
    tw->GetEventHandler()->ProcessEvent(event);
}

static void wxTextWindowActivateProc(Widget w, XtPointer clientData,
                                     XmAnyCallbackStruct *ptr)
{
    if (!wxGetWindowFromTable(w))
        return;

    wxTextCtrl *tw = (wxTextCtrl *) clientData;

    if (tw->InSetValue())
        return;

    wxCommandEvent event(wxEVT_COMMAND_TEXT_ENTER);
    event.SetId(tw->GetId());
    event.SetEventObject(tw);
    tw->ProcessCommand(event);
}

