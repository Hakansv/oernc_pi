/******************************************************************************
 *
 * Project:  
 * Purpose:   Plugin core
 * Author:   David Register
 *
 ***************************************************************************
 *   Copyright (C) 2019 by David S. Register   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************
 */


#include "wx/wxprec.h"

#ifndef  WX_PRECOMP
  #include "wx/wx.h"
#endif //precompiled headers

#include "piScreenLog.h"
#define ID_PISLCLOSE    11357

IMPLEMENT_DYNAMIC_CLASS( piScreenLogContainer, wxFrame )

//      Screen log container implementation
BEGIN_EVENT_TABLE(piScreenLogContainer, wxFrame)
//EVT_CLOSE(piScreenLogContainer::OnClose)
EVT_BUTTON(ID_PISLCLOSE, piScreenLogContainer::OnCloseClick)

END_EVENT_TABLE()

piScreenLogContainer::piScreenLogContainer()
{
}

piScreenLogContainer::piScreenLogContainer( wxWindow *parent, wxString title, wxSize size )
{
    wxSize tsize = wxSize(500,400);
    if( (size.x >0) && (size.y > 0) )
        tsize = size;
        
    wxFrame::Create( parent, -1, title, wxDefaultPosition, size, wxCAPTION | wxRESIZE_BORDER );
    m_slog = new piScreenLog( this );
    
    wxBoxSizer* itemBoxSizer2 = new wxBoxSizer( wxVERTICAL );
    SetSizer( itemBoxSizer2 );
 
    itemBoxSizer2->Add( m_slog, 1, wxEXPAND, 5 );
    

     // Close button
    wxButton* bClose = new wxButton( this, ID_PISLCLOSE, _( "Close" ), wxDefaultPosition, wxDefaultSize, 0 );
    //AckBox->Add( ack, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );
    itemBoxSizer2->Add(bClose);

    
    Hide();
}

piScreenLogContainer::~piScreenLogContainer()
{
    if( m_slog  ) 
        m_slog->Destroy();
}

void piScreenLogContainer::LogMessage(wxString s)
{
    if( m_slog  ) {
        m_slog->LogMessage( s );
        Show();
    }
}

void piScreenLogContainer::ClearLog(void)
{
    if( m_slog  ) {
        m_slog->ClearLog();
    }
}

void piScreenLogContainer::OnCloseClick(wxCommandEvent& event)
{
    ClearLog();
    Hide();
}




#define SERVER_ID       5000
#define SOCKET_ID       5001

BEGIN_EVENT_TABLE(piScreenLog, wxWindow)
EVT_SIZE(piScreenLog::OnSize)
EVT_SOCKET(SERVER_ID,  piScreenLog::OnServerEvent)
EVT_SOCKET(SOCKET_ID,  piScreenLog::OnSocketEvent)
END_EVENT_TABLE()

piScreenLog::piScreenLog(wxWindow *parent):
    wxWindow( parent, -1, wxDefaultPosition, wxDefaultSize)    
{
    
        

    Init();
    wxBoxSizer *LogSizer = new wxBoxSizer( wxVERTICAL );
    SetSizer( LogSizer );

    m_plogtc = new wxTextCtrl(this, -1, _T(""), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE );
    LogSizer->Add(m_plogtc, 1, wxEXPAND, 0);
    
}



piScreenLog::~piScreenLog()
{
     delete m_plogtc;

     StopServer();
}

void piScreenLog::Init()
{
    m_nseq = 0;
    m_plogtc = NULL;
    m_nseq = 0;
    
    m_server = NULL;
    m_backchannel_port = 0;
    m_server = NULL;
    m_bsuppress_log = false;
}

void piScreenLog::StartServer( unsigned int port)
{
    // Create a server socket to catch "back channel" messages
    
    m_backchannel_port = port;
    // Create the address - defaults to localhost:0 initially
    wxIPV4address addr;
    addr.Service(m_backchannel_port);
    addr.AnyAddress();
    
    // Create the socket
    m_server = new wxSocketServer(addr);
    
    // We use Ok() here to see if the server is really listening
    if (! m_server->Ok())
    {
        m_plogtc->AppendText(_("Log backchannel could not listen at the specified port !\n"));
    }
    else
    {
        m_plogtc->AppendText(_("Log backchannel server listening.\n\n"));
    }
    
    // Setup the event handler and subscribe to connection events
    m_server->SetEventHandler(*this, SERVER_ID);
    m_server->SetNotify(wxSOCKET_CONNECTION_FLAG);
    m_server->Notify(true);
        
}

void piScreenLog::StopServer( )
{
     if(m_server) {
        m_server->Notify(false);
        m_server->Destroy();
     }
}


void piScreenLog::OnSize( wxSizeEvent& event)
{
    Layout();
}

void piScreenLog::LogMessage(wxString s)
{
    if(s.IsEmpty())
        return;
        
    if( m_plogtc  ) {
        wxString seq;
        seq.Printf(_T("%6d: "), m_nseq++);
        
        wxString sp = s  + _T("\n") ;

        if(sp[0] == '\r'){
            int lp = m_plogtc->GetInsertionPoint();
            int nol = m_plogtc->GetNumberOfLines();
            int ll = m_plogtc->GetLineLength(nol-1);
            
            if(ll)
                m_plogtc->Remove(lp-ll, lp);
            m_plogtc->SetInsertionPoint(lp - ll );
            m_plogtc->WriteText(s.Mid(1));
            m_plogtc->SetInsertionPointEnd();
            
        }
        else {
            m_plogtc->AppendText(seq + sp);
//            m_plogtc->AppendText(sp);
        }
        
        Show();
        
//         if(gb_global_log)
//             g_logarray.Add(seq + sp);
        
    }
}

void piScreenLog::ClearLog(void)
{
    if(m_plogtc){
        m_plogtc->Clear();
    }
}

void piScreenLog::OnServerEvent(wxSocketEvent& event)
{
    wxString s; // = _("OnServerEvent: ");
    wxSocketBase *sock;
    
    switch(event.GetSocketEvent())
    {
        case wxSOCKET_CONNECTION :
//            s.Append(_("wxSOCKET_CONNECTION\n"));
            break;
        default                  :
            s.Append(_("Unexpected event !\n"));
            break;
    }
    
    m_plogtc->AppendText(s + _T("\n") );
    
    // Accept new connection if there is one in the pending
    // connections queue, else exit. We use Accept(false) for
    // non-blocking accept (although if we got here, there
    // should ALWAYS be a pending connection).
    
    sock = m_server->Accept(false);
    
    if (sock)
    {
//       m_plogtc->AppendText(_("New client connection accepted\n\n"));
    }
    else
    {
        m_plogtc->AppendText(_("Error: couldn't accept a new connection\n\n"));
        return;
    }
    
    sock->SetEventHandler(*this, SOCKET_ID);
    sock->SetNotify(wxSOCKET_INPUT_FLAG | wxSOCKET_LOST_FLAG);
    sock->Notify(true);
#ifdef __WXGTK__
    sock->SetFlags(wxSOCKET_NONE);
#else    
    sock->SetFlags(wxSOCKET_BLOCK);
#endif    
    
    
}

void piScreenLog::OnSocketEvent(wxSocketEvent& event)
{
    wxString s; // = _("OnSocketEvent: ");
    wxSocketBase *sock = event.GetSocket();
    
    // First, print a message
    switch(event.GetSocketEvent())
    {
        case wxSOCKET_INPUT : 
//            s.Append(_("wxSOCKET_INPUT\n"));
            break;
        case wxSOCKET_LOST  :
//            s.Append(_("wxSOCKET_LOST\n"));
            break;
        default             :
            s.Append(_("Unexpected event !\n"));
            break;
    }
    
    m_plogtc->AppendText(s);
    
    // Now we process the event
    switch(event.GetSocketEvent())
    {
        case wxSOCKET_INPUT:
        {
            // We disable input events, so that the test doesn't trigger
            // wxSocketEvent again.
            sock->SetNotify(wxSOCKET_LOST_FLAG);
            
            char buf[160];
            
            sock->ReadMsg( buf, sizeof(buf) );
            size_t rlen = sock->LastCount();
            if(rlen < sizeof(buf))
                buf[rlen] = '\0';
            else
                buf[0] = '\0';
            
            if(rlen) {
                wxString msg(buf, wxConvUTF8);
                if(!m_bsuppress_log)
                    LogMessage(msg);
            }
            
            // Enable input events again.
            sock->SetNotify(wxSOCKET_LOST_FLAG | wxSOCKET_INPUT_FLAG);
            break;
        }
                case wxSOCKET_LOST:
                {
                    
                    // Destroy() should be used instead of delete wherever possible,
                    // due to the fact that wxSocket uses 'delayed events' (see the
                    // documentation for wxPostEvent) and we don't want an event to
                    // arrive to the event handler (the frame, here) after the socket
                    // has been deleted. Also, we might be doing some other thing with
                    // the socket at the same time; for example, we might be in the
                    // middle of a test or something. Destroy() takes care of all
                    // this for us.
                    
//                    m_plogtc->AppendText(_("Deleting socket.\n\n"));

                    sock->Destroy();
                    break;
                }
                default: ;
    }
    
}

