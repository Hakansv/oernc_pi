/******************************************************************************
 * $Id: oernc_pi.cpp,v 1.8 2010/06/21 01:54:37 bdbcat Exp $
 *
 * Project:  OpenCPN
 * Purpose:  XTR1 PlugIn
 * Author:   David Register
 *
 ***************************************************************************
 *   Copyright (C) 2018 by David S. Register                               *
 *   $EMAIL$                                                               *
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
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/fileconf.h>

#include "oernc_pi.h"
#include "chart.h"
#include "oernc_inStream.h"
#include "ochartShop.h"
#include <map>
#include <unordered_map>
#include <tinyxml.h>

#ifdef __OCPN__ANDROID__
#include "androidSupport.h"
#include "qdebug.h"
#endif

// the class factories, used to create and destroy instances of the PlugIn

extern "C" DECL_EXP opencpn_plugin* create_pi(void *ppimgr)
{
    return new oernc_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p)
{
    delete p;
}


wxString g_server_bin;
wxString g_pipeParm;
bool g_serverDebug;
long g_serverProc;
int g_debugLevel = 0;
int g_admin;

wxString  g_deviceInfo;
extern wxString  g_loginUser;
extern wxString  g_PrivateDataDir;
wxString  g_versionString;
bool g_bNoFindMessageShown;
extern wxString g_systemName;
extern wxString g_loginKey;
wxString  g_fpr_file;

//std::unordered_map<std::string, std::string> keyMap;
WX_DECLARE_STRING_HASH_MAP( wxString, OKeyHash );
OKeyHash keyMapDongle;
OKeyHash keyMapSystem;

OKeyHash *pPrimaryKey;
OKeyHash *pAlternateKey;

oerncPrefsDialog  *g_prefs_dialog;
oernc_pi_event_handler         *g_event_handler;
    
//---------------------------------------------------------------------------------------------------------
//
//    PlugIn Implementation
//
//---------------------------------------------------------------------------------------------------------

#include "default_pi.xpm"

bool parseKeyFile( wxString kfile, bool bDongle )
{
    FILE *iFile = fopen(kfile.mb_str(), "rb");
   
    if (iFile <= 0)
        return false;            // file error
        
    // compute the file length    
    fseek(iFile, 0, SEEK_END);
    size_t iLength = ftell(iFile);
    
    char *iText = (char *)calloc(iLength + 1, sizeof(char));
    
    // Read the file
    fseek(iFile, 0, SEEK_SET);
    size_t nread = 0;
    while (nread < iLength){
        nread += fread(iText + nread, 1, iLength - nread, iFile);
    }           
    fclose(iFile);

    
    //  Parse the XML
    TiXmlDocument * doc = new TiXmlDocument();
    const char *rr = doc->Parse( iText);
    
    TiXmlElement * root = doc->RootElement();
    if(!root)
        return false;                              // undetermined error??

    wxString RInstallKey, fileName;
    wxString rootName = wxString::FromUTF8( root->Value() );
    if(rootName.IsSameAs(_T("keyList"))){
            
        TiXmlNode *child;
        for ( child = root->FirstChild(); child != 0; child = child->NextSibling()){
            wxString s = wxString::FromUTF8(child->Value());  //chart
            
            TiXmlNode *childChart = child->FirstChild();
            for ( childChart = child->FirstChild(); childChart!= 0; childChart = childChart->NextSibling()){
                const char *chartVal =  childChart->Value();
                            
                if(!strcmp(chartVal, "RInstallKey")){
                    TiXmlNode *childVal = childChart->FirstChild();
                    if(childVal){
                        RInstallKey = childVal->Value();
                        
                    }
                }
                if(!strcmp(chartVal, "FileName")){
                    TiXmlNode *childVal = childChart->FirstChild();
                    if(childVal){
                        fileName = childVal->Value();
                        
                    }
                }

            }
            if(RInstallKey.Length() && fileName.Length()){
                if(bDongle){
                    OKeyHash::iterator search = keyMapDongle.find(fileName);
                    if (search == keyMapDongle.end()) {
                        keyMapDongle[fileName] = RInstallKey;
                    }
                }
                else{
                    OKeyHash::iterator search = keyMapSystem.find(fileName);
                    if (search == keyMapSystem.end()) {
                        keyMapSystem[fileName] = RInstallKey;
                    }
                }
            }
        }
        
        free( iText );
        return true;
    }           

    free( iText );
    return false;

}
    

bool loadKeyMaps( wxString file )
{
    // Make a list of all XML or xml files found in the parent directory of the chart itself.
    wxFileName fn(file);

    wxArrayString xmlFiles;
    int nFiles = wxDir::GetAllFiles(fn.GetPath(), &xmlFiles, _T("*.XML"));
    nFiles += wxDir::GetAllFiles(fn.GetPath(), &xmlFiles, _T("*.xml"));
        
    //  Read and parse them all
    for(unsigned int i=0; i < xmlFiles.GetCount(); i++){
        wxString xmlFile = xmlFiles.Item(i);
        // Is this a dongle key file?
        if(wxNOT_FOUND != xmlFile.Find(_T("-sgl")))
            parseKeyFile(xmlFile, true);
        else
            parseKeyFile(xmlFile, false);
    }
    
 
    return true;
}


wxString getPrimaryKey(wxString file)
{
    if(pPrimaryKey){
        wxFileName fn(file);
        OKeyHash::iterator search = pPrimaryKey->find(fn.GetName());
        if (search != pPrimaryKey->end()) {
            return search->second;
        }

        loadKeyMaps(file);

        search = pPrimaryKey->find(fn.GetName());
        if (search != pPrimaryKey->end()) {
            return search->second;
        }
    }
    return wxString();
}

wxString getAlternateKey(wxString file)
{
    if(pAlternateKey){
        wxFileName fn(file);
        OKeyHash::iterator search = pAlternateKey->find(fn.GetName());
        if (search != pAlternateKey->end()) {
            return search->second;
        }

        loadKeyMaps(file);

        search = pAlternateKey->find(fn.GetName());
        if (search != pAlternateKey->end()) {
            return search->second;
        }
    }
    return wxString();
}

void SwapKeyHashes()
{
    OKeyHash *tmp = pPrimaryKey;
    pPrimaryKey = pAlternateKey;
    pAlternateKey = tmp;
}

    
//---------------------------------------------------------------------------------------------------------
//
//          PlugIn initialization and de-init
//
//---------------------------------------------------------------------------------------------------------

oernc_pi::oernc_pi(void *ppimgr)
      :opencpn_plugin_19(ppimgr)
{
      // Create the PlugIn icons

      m_pplugin_icon = new wxBitmap(default_pi);
}

oernc_pi::~oernc_pi()
{
      delete m_pplugin_icon;
}

int oernc_pi::Init(void)
{
    wxString vs;
    vs.Printf(_T("%d.%d.%d"), PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_PATCH);
    g_versionString = vs;

    m_shoppanel = NULL;

    g_event_handler = new oernc_pi_event_handler(this);

    AddLocaleCatalog( _T("opencpn-oernc_pi") );

      //    Build an arraystring of dynamically loadable chart class names
      m_class_name_array.Add(_T("Chart_oeRNC"));

      // Specify the location of the xxserverd helper.
      wxFileName fn_exe(GetOCPN_ExePath());
      g_server_bin = fn_exe.GetPath( wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + _T("oeaserverd");
      
      
      #ifdef __WXMSW__
      g_server_bin = _T("\"") + fn_exe.GetPath( wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) +
      _T("plugins\\oernc_pi\\oeaserverd.exe\"");
      #endif
      
      #ifdef __WXOSX__
      fn_exe.RemoveLastDir();
      g_server_bin = _T("\"") + fn_exe.GetPath( wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) +
      _T("PlugIns/oernc_pi/oeaserverd\"");
      #endif
      
      #ifdef __OCPN__ANDROID__
      wxString piLocn = GetPlugInPath(this); //*GetpSharedDataLocation();
      wxFileName fnl(piLocn);
      g_server_bin = fnl.GetPath(wxPATH_GET_SEPARATOR) + _T("oeaserverda");
      g_serverProc = 0;
      #endif
      
      
      // Get and build if necessary a private data dir
      g_PrivateDataDir = *GetpPrivateApplicationDataLocation();
      g_PrivateDataDir += wxFileName::GetPathSeparator();
      g_PrivateDataDir += _T("oernc_pi");
      g_PrivateDataDir += wxFileName::GetPathSeparator();
      if(!::wxDirExists( g_PrivateDataDir ))
          ::wxMkdir( g_PrivateDataDir );
      
      wxLogMessage(_T("Path to serverd is: ") + g_server_bin);
      
      int flags = INSTALLS_PLUGIN_CHART;

      flags |= INSTALLS_TOOLBOX_PAGE;             // for  shop interface
      flags |= WANTS_PREFERENCES;             

      // Set up the initial key hash table pointers
      pPrimaryKey = &keyMapDongle;
      pAlternateKey = &keyMapSystem;

      return flags;
      
}

bool oernc_pi::DeInit(void)
{
#if 0    
    SaveConfig();
    
    delete pinfoDlg;
    pinfoDlg = NULL;
    
    if( m_pOptionsPage )
    {
        if( DeleteOptionsPage( m_pOptionsPage ) )
            m_pOptionsPage = NULL;
    }
#endif

    m_class_name_array.Clear();
    
    shutdown_server();
    
    return true;
}

int oernc_pi::GetAPIVersionMajor()
{
      return MY_API_VERSION_MAJOR;
}

int oernc_pi::GetAPIVersionMinor()
{
      return MY_API_VERSION_MINOR;
}

int oernc_pi::GetPlugInVersionMajor()
{
      return PLUGIN_VERSION_MAJOR;
}

int oernc_pi::GetPlugInVersionMinor()
{
      return PLUGIN_VERSION_MINOR;
}

wxBitmap *oernc_pi::GetPlugInBitmap()
{
      return m_pplugin_icon;
}

wxString oernc_pi::GetCommonName()
{
    return _("oeRNC Charts");
}


wxString oernc_pi::GetShortDescription()
{
    return _("oeRNC Charts PlugIn for OpenCPN");
}


wxString oernc_pi::GetLongDescription()
{
      return _("oeRNC Charts PlugIn for OpenCPN\n\
Provides support for oeRNC raster charts.\n\n\
");

}

wxArrayString oernc_pi::GetDynamicChartClassNameArray()
{
      return m_class_name_array;
}


void oernc_pi::OnSetupOptions( void )
{
#ifdef __OCPN__ANDROID__
    m_pOptionsPage = AddOptionsPage( PI_OPTIONS_PARENT_CHARTS, _("oeRNC Charts") );
    if( ! m_pOptionsPage )
    {
        wxLogMessage( _T("Error: oernc_pi::OnSetupOptions AddOptionsPage failed!") );
        return;
    }
    wxBoxSizer *sizer = new wxBoxSizer( wxVERTICAL );
    m_pOptionsPage->SetSizer( sizer );
    
    m_oesencpanel = new oesencPanel( this, m_pOptionsPage, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE );

    m_pOptionsPage->InvalidateBestSize();
    sizer->Add( m_oesencpanel, 1, wxALL | wxEXPAND );
    m_oesencpanel->FitInside();
#else
    m_pOptionsPage = AddOptionsPage( PI_OPTIONS_PARENT_CHARTS, _("oeRNC Charts") );
    if( ! m_pOptionsPage )
    {
        wxLogMessage( _T("Error: oernc_pi::OnSetupOptions AddOptionsPage failed!") );
        return;
    }
    wxBoxSizer *sizer = new wxBoxSizer( wxVERTICAL );
    m_pOptionsPage->SetSizer( sizer );
    
    m_shoppanel = new shopPanel( m_pOptionsPage, wxID_ANY, wxDefaultPosition, wxDefaultSize );
    
    m_pOptionsPage->InvalidateBestSize();
    sizer->Add( m_shoppanel, 1, wxALL | wxEXPAND );
    m_shoppanel->FitInside();

    
#endif
}
void oernc_pi::OnCloseToolboxPanel(int page_sel, int ok_apply_cancel)
{
    if(m_shoppanel){
        //m_shoppanel->StopAllDownloads();
    }
    
}

void oernc_pi::ShowPreferencesDialog( wxWindow* parent )
{
    wxString titleString =  _("oeRNC_PI Preferences");

    long style = wxDEFAULT_DIALOG_STYLE;
#ifdef __WXOSX__
        style |= wxSTAY_ON_TOP;
#endif

    g_prefs_dialog = new oerncPrefsDialog( parent, wxID_ANY, titleString, wxPoint( 20, 20), wxDefaultSize, style );
    g_prefs_dialog->Fit();
//    g_prefs_dialog->SetSize(wxSize(300, -1));
    //wxColour cl;
    //GetGlobalColor(_T("DILG1"), &cl);
//    g_prefs_dialog->SetBackgroundColour(cl);
    
    
    g_prefs_dialog->Show();
        
    if(g_prefs_dialog->ShowModal() == wxID_OK)
    {
        saveShopConfig();
        
    }
    delete g_prefs_dialog;
    g_prefs_dialog = NULL;
}

void oernc_pi::Set_FPR()
{
    g_prefs_dialog->EndModal( wxID_OK );
    g_prefs_dialog->m_buttonShowFPR->Enable( g_fpr_file != wxEmptyString );
}



bool validate_server(void)
{
    
    
    if(g_debugLevel)printf("\n-------validate_server\n");
    //wxLogMessage(_T("validate_server"));
    
    if(1 /*g_serverProc*/){
        // Check to see if the server is already running, and available
        //qDebug() << "Check running server Proc";
        oernc_inStream testAvail;
        if(testAvail.isAvailable(wxEmptyString)){
            //wxLogMessage(_T("Available TRUE"));
            return true;
        }
        
        wxString tmsg;
        int nLoop = 1;
        while(nLoop < 2){
            tmsg.Printf(_T(" nLoop: %d"), nLoop);
            if(g_debugLevel)printf("      validate_server, retry: %d \n", nLoop);
            wxLogMessage(_T("Available FALSE, retrying...") + tmsg);
            wxMilliSleep(500);
            oernc_inStream testAvailRetry;
            if(testAvailRetry.isAvailable(wxEmptyString)){
                wxLogMessage(_T("Available TRUE on retry."));
                return true;
            }
            nLoop++;
        }
    }
    
    // Not running, so start it up...
    
    wxString bin_test = g_server_bin;
    
#ifndef __OCPN__ANDROID__    
    //Verify that serverd actually exists, and runs.
    
    if(wxNOT_FOUND != g_server_bin.Find('\"'))
        bin_test = g_server_bin.Mid(1).RemoveLast();
    
    wxString msg = _T("Checking server utility at ");
    msg += _T("{");
    msg += bin_test;
    msg += _T("}");
    wxLogMessage(_T("oernc_pi: ") + msg);
    
    
    if(!::wxFileExists(bin_test)){
        if(!g_bNoFindMessageShown){
            wxString msg = _("Cannot find the oernc_pi server utility at \n");
            msg += _T("{");
            msg += bin_test;
            msg += _T("}");
            OCPNMessageBox_PlugIn(NULL, msg, _("oernc_pi Message"),  wxOK, -1, -1);
            wxLogMessage(_T("oernc_pi: ") + msg);
        
            g_bNoFindMessageShown = true;
        }
        g_server_bin.Clear();
        return false;
    }
    
    // now start the server...
    wxString cmds = g_server_bin;
    
    
    wxString pipeParm;
    
    int flags = wxEXEC_ASYNC;
#ifdef __WXMSW__    
    flags |= wxEXEC_HIDE_CONSOLE;
    long pid = ::wxGetProcessId();
    pipeParm.Printf(_T("OCPN_PIPER%04d"), pid % 10000);
    g_pipeParm = pipeParm;
#endif
    
    if(g_pipeParm.Length())
        cmds += _T(" -p ") + g_pipeParm;
    
    if(g_serverDebug)
        cmds += _T(" -d");
    
    wxLogMessage(_T("oernc_pi: starting serverd utility: ") + cmds);
    g_serverProc = wxExecute(cmds, flags);              // exec asynchronously
    wxMilliSleep(1000);
    
    
#else           // Android
    qDebug() << "Starting oernc_pi server";
    
    //  The target binary executable
    wxString cmd = g_server_bin;
    
    //  Set up the parameter passed as the local app storage directory
    wxString dataLoc = *GetpPrivateApplicationDataLocation();
    wxFileName fn(dataLoc);
    wxString dataDir = fn.GetPath(wxPATH_GET_SEPARATOR);
    
    //  Set up the parameter passed to runtime environment as LD_LIBRARY_PATH
    // This will be {dir of g_server_bin}/lib
    wxFileName fnl(cmd);
    wxString libDir = fnl.GetPath(wxPATH_GET_SEPARATOR) + _T("lib");
    
    wxLogMessage(_T("oernc_pi: Starting: ") + cmd );
    
    wxString result = callActivityMethod_s4s("createProc", cmd, _T("-q"), dataDir, libDir);
    
    wxLogMessage(_T("oernc_pi: Start Result: ") + result);
    
    long pid;
    if(result.ToLong(&pid))
        g_serverProc = pid;
    
    wxMilliSleep(1000);
    
#endif    
    
    // Check to see if the server function is available
    if(g_serverProc){
        bool bAvail = false;
        int nLoop = 10;
        
        while(nLoop){
            oernc_inStream testAvail_One;
            if(!testAvail_One.isAvailable(_T("?")))
                wxSleep(1);
            else{
                bAvail = true;
                break;
            }
            nLoop--;
        }
        
        if(!bAvail){
            wxString msg = _T("oeaserverd utility at \n");
            msg += _T("{");
            msg += bin_test;
            msg += _T("}\n");
            msg += _T(" reports Unavailable.\n\n");
            //            OCPNMessageBox_PlugIn(NULL, msg, _("oeRNC_PI Message"),  wxOK, -1, -1);
            wxLogMessage(_T("oernc_pi: ") + msg);
            
            g_server_bin.Clear();
            return false;
            
        }
        else{
            wxString nc;
            nc.Printf(_T("LoopCount: %d"), nLoop);
            
            wxLogMessage(_T("oernc_pi: serverd Check OK...") + nc);

        }
    }
    else{
        wxString msg = _("serverd utility at \n");
        msg += _T("{");
        msg += bin_test;
        msg += _T("}\n");
        msg += _(" could not be started.\n\n");
        OCPNMessageBox_PlugIn(NULL, msg, _("oernc_pi Message"),  wxOK, -1, -1);
        wxLogMessage(_T("oernc_pi: ") + msg);
        
        g_server_bin.Clear();
        return false;
    }
    
    return true;
}

bool shutdown_server( void )
{
    wxLogMessage(_T("oernc_pi: Shutdown_server"));
    
    // Check to see if the server is already running, and available
    oernc_inStream testAvail;
    if(1){
        testAvail.Shutdown();
        return true;
    }
    else{
        return false;
    }
}


BEGIN_EVENT_TABLE( oerncPrefsDialog, wxDialog )
EVT_BUTTON( wxID_OK, oerncPrefsDialog::OnPrefsOkClick )
END_EVENT_TABLE()

oerncPrefsDialog::oerncPrefsDialog( wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style )
{
    wxDialog::Create( parent, id, title, pos, size, style );
    
#ifdef __OCPN__ANDROID__
    SetBackgroundColour(ANDROID_DIALOG_BACKGROUND_COLOR);
#endif    
    
        this->SetSizeHints( wxDefaultSize, wxDefaultSize );
    
        wxBoxSizer* bSizerTop = new wxBoxSizer( wxVERTICAL );
        
        wxPanel *content = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBG_STYLE_ERASE );
        bSizerTop->Add(content, 0, wxALL|wxEXPAND, WXC_FROM_DIP(10));
        
        wxBoxSizer* bSizer2 = new wxBoxSizer( wxVERTICAL );
        content->SetSizer(bSizer2);
        
        // Plugin Version
        wxString versionText = _(" oeSENC Version: ") + g_versionString;
        wxStaticText *versionTextBox = new wxStaticText(content, wxID_ANY, versionText);
        bSizer2->Add(versionTextBox, 1, wxALL | wxALIGN_CENTER_HORIZONTAL, 20 );
 
        //  Show EULA
        m_buttonShowEULA = new wxButton( content, wxID_ANY, _("Show EULA"), wxDefaultPosition, wxDefaultSize, 0 );
        bSizer2->AddSpacer( 10 );
        bSizer2->Add( m_buttonShowEULA, 0, wxALIGN_CENTER_HORIZONTAL, 50 );
        m_buttonShowEULA->Connect( wxEVT_COMMAND_BUTTON_CLICKED,wxCommandEventHandler(oernc_pi_event_handler::OnShowEULA), NULL, g_event_handler );
        bSizer2->AddSpacer( 20 );

#ifndef __OCPN__ANDROID__        
        //  FPR File Permit
        wxStaticBoxSizer* sbSizerFPR= new wxStaticBoxSizer( new wxStaticBox( content, wxID_ANY, _("System Identification") ), wxHORIZONTAL );
        m_fpr_text = new wxStaticText(content, wxID_ANY, _T(" "));
        if(g_fpr_file.Len())
             m_fpr_text->SetLabel( wxFileName::FileName(g_fpr_file).GetFullName() );
        else
             m_fpr_text->SetLabel( _T("                  "));
         
        sbSizerFPR->Add(m_fpr_text, wxEXPAND);
        bSizer2->Add(sbSizerFPR, 0, wxEXPAND, 50 );

        m_buttonNewFPR = new wxButton( content, wxID_ANY, _("Create System Identifier file..."), wxDefaultPosition, wxDefaultSize, 0 );
        
        bSizer2->AddSpacer( 5 );
        bSizer2->Add( m_buttonNewFPR, 0, wxALIGN_CENTER_HORIZONTAL, 50 );
        
        m_buttonNewFPR->Connect( wxEVT_COMMAND_BUTTON_CLICKED,wxCommandEventHandler(oernc_pi_event_handler::OnNewFPRClick), NULL, g_event_handler );

#ifndef OCPN_ARM64        
        m_buttonNewDFPR = new wxButton( content, wxID_ANY, _("Create USB key dongle System ID file..."), wxDefaultPosition, wxDefaultSize, 0 );
        
        bSizer2->AddSpacer( 5 );
        bSizer2->Add( m_buttonNewDFPR, 0, wxALIGN_CENTER_HORIZONTAL, 50 );
        
        m_buttonNewDFPR->Connect( wxEVT_COMMAND_BUTTON_CLICKED,wxCommandEventHandler(oernc_pi_event_handler::OnNewDFPRClick), NULL, g_event_handler );
#endif
            
#ifdef __WXMAC__
        m_buttonShowFPR = new wxButton( content, wxID_ANY, _("Show In Finder"), wxDefaultPosition, wxDefaultSize, 0 );
#else
        m_buttonShowFPR = new wxButton( content, wxID_ANY, _("Show on disk"), wxDefaultPosition, wxDefaultSize, 0 );
#endif
        bSizer2->AddSpacer( 20 );
        bSizer2->Add( m_buttonShowFPR, 0, wxALIGN_CENTER_HORIZONTAL, 50 );

        m_buttonShowFPR->Enable( g_fpr_file != wxEmptyString );

        m_buttonShowFPR->Connect( wxEVT_COMMAND_BUTTON_CLICKED,wxCommandEventHandler(oernc_pi_event_handler::OnShowFPRClick), NULL, g_event_handler );

#endif        
        // System Name
        if(g_systemName.Length()){
            wxString nameText = _T(" ") + _("System Name:") + _T(" ") + g_systemName;
            m_nameTextBox = new wxStaticText(content, wxID_ANY, nameText);
            bSizer2->AddSpacer( 20 );
            bSizer2->Add(m_nameTextBox, 1, wxTOP | wxBOTTOM | wxALIGN_CENTER_HORIZONTAL, 10 );
        }
        else
            bSizer2->AddSpacer( 10 );
 
#ifndef __OCPN__ANDROID__        
        m_buttonClearSystemName = new wxButton( content, wxID_ANY, _("Reset System Name"), wxDefaultPosition, wxDefaultSize, 0 );
        
        bSizer2->AddSpacer( 10 );
        bSizer2->Add( m_buttonClearSystemName, 0, wxALIGN_CENTER_HORIZONTAL, 50 );
        
        m_buttonClearSystemName->Connect( wxEVT_COMMAND_BUTTON_CLICKED,wxCommandEventHandler(oernc_pi_event_handler::OnClearSystemName), NULL, g_event_handler );
        
        if(!g_systemName.Length())
            m_buttonClearSystemName->Disable();
        
        m_buttonClearCreds = new wxButton( content, wxID_ANY, _("Reset o-charts credentials"), wxDefaultPosition, wxDefaultSize, 0 );
        
        bSizer2->AddSpacer( 10 );
        bSizer2->Add( m_buttonClearCreds, 0, wxALIGN_CENTER_HORIZONTAL, 50 );
        
        m_buttonClearCreds->Connect( wxEVT_COMMAND_BUTTON_CLICKED,wxCommandEventHandler(oernc_pi_event_handler::OnClearCredentials), NULL, g_event_handler );

        m_buttonClearDownloadCache = new wxButton( content, wxID_ANY, _("Clear file download cache"), wxDefaultPosition, wxDefaultSize, 0 );
        bSizer2->AddSpacer( 10 );
        bSizer2->Add( m_buttonClearDownloadCache, 0, wxALIGN_CENTER_HORIZONTAL, 50 );
        
        m_buttonClearDownloadCache->Connect( wxEVT_COMMAND_BUTTON_CLICKED,wxCommandEventHandler(oernc_pi_event_handler::OnClearDownloadCache), NULL, g_event_handler );
        
        
#endif
            
        m_sdbSizer1 = new wxStdDialogButtonSizer();
        m_sdbSizer1OK = new wxButton( content, wxID_OK );
        m_sdbSizer1->AddButton( m_sdbSizer1OK );
        m_sdbSizer1Cancel = new wxButton( content, wxID_CANCEL );
        m_sdbSizer1->AddButton( m_sdbSizer1Cancel );
        m_sdbSizer1->Realize();
        
        bSizer2->Add( m_sdbSizer1, 0, wxBOTTOM|wxEXPAND|wxTOP, 20 );
        
        
        this->SetSizer( bSizerTop );
        this->Layout();
        bSizerTop->Fit( this );
        
        this->Centre( wxBOTH );
}

oerncPrefsDialog::~oerncPrefsDialog()
{
}

void oerncPrefsDialog::OnPrefsOkClick(wxCommandEvent& event)
{
#if 0    
    m_trackedPointName = m_wpComboPort->GetValue();
    
    wxArrayString guidArray = GetWaypointGUIDArray();
    for(unsigned int i=0 ; i < guidArray.GetCount() ; i++){
        wxString name = getWaypointName( guidArray[i] );
        if(name.Length()){
            if(name.IsSameAs(m_trackedPointName)){
                m_trackedPointGUID = guidArray[i];
                break;
            }
        }
    }
#endif
    EndModal( wxID_OK );
 
}

// An Event handler class to catch events from UI dialog
//      Implementation
#define ANDROID_EVENT_TIMER 4392
#define ACTION_ARB_RESULT_POLL 1

BEGIN_EVENT_TABLE ( oernc_pi_event_handler, wxEvtHandler )
EVT_TIMER ( ANDROID_EVENT_TIMER, oernc_pi_event_handler::onTimerEvent )
END_EVENT_TABLE()

oernc_pi_event_handler::oernc_pi_event_handler(oernc_pi *parent)
{
    m_parent = parent;
    m_eventTimer.SetOwner( this, ANDROID_EVENT_TIMER );
    m_timerAction = -1;
    
}

oernc_pi_event_handler::~oernc_pi_event_handler()
{
}

void oernc_pi_event_handler::onTimerEvent(wxTimerEvent &event)
{
#ifdef __OCPN__ANDROID__    
    if(ACTION_ARB_RESULT_POLL == m_timerAction){
        wxString status = callActivityMethod_vs("getArbActivityStatus");
        //qDebug() << status.mb_str();
        
        if(status == _T("COMPLETE")){
            m_eventTimer.Stop();
            m_timerAction = -1;
            
            qDebug() << "Got COMPLETE";
            wxString result = callActivityMethod_vs("getArbActivityResult");
            qDebug() << result.mb_str();
            processArbResult(result);
        }
    }
#endif    
}

void oernc_pi_event_handler::processArbResult( wxString result )
{
//    m_parent->ProcessChartManageResult(result);
}


void oernc_pi_event_handler::OnShowFPRClick( wxCommandEvent &event )
{
#ifdef __WXMAC__
    wxExecute( wxString::Format("open -R %s", g_fpr_file) );
#endif
#ifdef __WXMSW__                 
    wxExecute( wxString::Format("explorer.exe /select,%s", g_fpr_file) );
#endif
#ifdef __WXGTK__
    wxExecute( wxString::Format("xdg-open %s", wxFileName::FileName(g_fpr_file).GetPath()) );
#endif
}

void oernc_pi_event_handler::OnClearSystemName( wxCommandEvent &event )
{
    wxString msg = _("System name RESET shall be performed only by request from o-charts technical support staff.");
    msg += _T("\n\n");
    msg += _("Proceed to RESET?");
    int ret = OCPNMessageBox_PlugIn(NULL, msg, _("oeRNC_PI Message"), wxYES_NO);
    
    if(ret != wxID_YES)
        return;
        
    g_systemName.Clear();
    if(g_prefs_dialog){
        g_prefs_dialog->m_nameTextBox->SetLabel(_T(" "));
        g_prefs_dialog->m_buttonClearSystemName->Disable();
        
        g_prefs_dialog->Refresh(true);
    }
    wxFileConfig *pConf = GetOCPNConfigObject();
    if( pConf ) {
        pConf->SetPath( _T("/PlugIns/oernc") );
        pConf->Write( _T("systemName"), g_systemName);
    }
    
#ifndef __OCPN__ANDROID__    
    if(m_parent->m_shoppanel){
        m_parent->m_shoppanel->RefreshSystemName();
    }
#endif    
        
}

void oernc_pi_event_handler::OnClearDownloadCache( wxCommandEvent &event )
{
    wxString cache_locn = wxString(g_PrivateDataDir + _T("DownloadCache"));
    if(wxDir::Exists(cache_locn)){
        wxArrayString fileArray;
        size_t nFiles = wxDir::GetAllFiles( cache_locn, &fileArray);
        for(unsigned int i=0 ; i < nFiles ;i++)
            ::wxRemoveFile(fileArray.Item(i));
    }
    
    wxString msg = _("Download file cache cleared.");
    OCPNMessageBox_PlugIn(NULL, msg, _("oeRNC_PI Message"), wxOK);
    

}


void oernc_pi_event_handler::OnShowEULA( wxCommandEvent &event )
{
#if 0
    ChartSetEULA *CSE;
    
    for(unsigned int i=0 ; i < g_EULAArray.GetCount() ; i++){
        CSE = g_EULAArray.Item(i);
        wxString file = CSE->fileName;
        file.Replace('!', wxFileName::GetPathSeparator());
        
        if(wxFileExists(file)){
            oesenc_pi_about *pab = new oesenc_pi_about( GetOCPNCanvasWindow(), file );
            pab->SetOKMode();
            pab->ShowModal();
            pab->Destroy();
        
            break;                      // once is enough
        }
    }
#endif    
}

extern void saveShopConfig();

void oernc_pi_event_handler::OnClearCredentials( wxCommandEvent &event )
{
    g_loginKey.Clear();
    saveShopConfig();
    
    OCPNMessageBox_PlugIn(NULL, _("Credential Reset Successful"), _("oeRNC_PI Message"), wxOK);
 
}

void oernc_pi_event_handler::OnNewDFPRClick( wxCommandEvent &event )
{
#ifndef __OCPN__ANDROID__    
    wxString msg = _("To obtain a chart set, you must generate a Unique System Identifier File.\n");
    msg += _("This file is also known as a\"fingerprint\" file.\n");
    msg += _("The fingerprint file contains information related to a connected USB key dongle.\n\n");
    msg += _("After creating this file, you will need it to obtain your chart sets at the o-charts.org shop.\n\n");
    msg += _("Proceed to create Fingerprint file?");


    int ret = OCPNMessageBox_PlugIn(NULL, msg, _("oeRNC_PI Message"), wxYES_NO);
    
    if(ret == wxID_YES){
        wxString msg1;
        
        bool b_copyOK = false;
        wxString fpr_file = getFPR( true , b_copyOK, true);
        
        // Check for missing dongle...
        if(fpr_file.IsSameAs(_T("DONGLE_NOT_PRESENT"))){
            OCPNMessageBox_PlugIn(NULL, _("ERROR Creating Fingerprint file\n USB key dongle not detected."), _("oeRNC_PI Message"), wxOK);
            return;
        }
        
        if(fpr_file.Len()){
            msg1 += _("Fingerprint file created.\n");
            msg1 += fpr_file;
            
            if(b_copyOK)
                msg1 += _("\n\n Fingerprint file is also copied to desktop.");
            
            OCPNMessageBox_PlugIn(NULL, msg1, _("oeRNC_PI Message"), wxOK);
            
            m_parent->Set_FPR();
            
        }
        else{
            OCPNMessageBox_PlugIn(NULL, _("ERROR Creating Fingerprint file\n Check OpenCPN log file."), _("oeRNC_PI Message"), wxOK);
        }
        
        g_fpr_file = fpr_file;
        
    }           // yes
#endif
}



void oernc_pi_event_handler::OnNewFPRClick( wxCommandEvent &event )
{
#ifndef __OCPN__ANDROID__    
    wxString msg = _("To obtain a chart set, you must generate a Unique System Identifier File.\n");
    msg += _("This file is also known as a\"fingerprint\" file.\n");
    msg += _("The fingerprint file contains information to uniquely identify this computer.\n\n");
    msg += _("After creating this file, you will need it to obtain your chart sets at the o-charts.org shop.\n\n");
    msg += _("Proceed to create Fingerprint file?");

    int ret = OCPNMessageBox_PlugIn(NULL, msg, _("oeRNC_PI Message"), wxYES_NO);
    
    if(ret == wxID_YES){
        wxString msg1;
        
        bool b_copyOK = false;
        wxString fpr_file = getFPR( true , b_copyOK, false);
        
        if(fpr_file.Len()){
            msg1 += _("Fingerprint file created.\n");
            msg1 += fpr_file;
            
            if(b_copyOK)
                msg1 += _("\n\n Fingerprint file is also copied to desktop.");
            
            OCPNMessageBox_PlugIn(NULL, msg1, _("oeRNC_PI Message"), wxOK);
            
            m_parent->Set_FPR();
            
        }
        else{
            OCPNMessageBox_PlugIn(NULL, _T("ERROR Creating Fingerprint file\n Check OpenCPN log file."), _("oeRNC_PI Message"), wxOK);
        }
        
        g_fpr_file = fpr_file;
        
    }           // yes
#else                   // Android

        // Get XFPR from the oeserverda helper utility.
        //  The target binary executable
        wxString cmd = g_sencutil_bin;

//  Set up the parameter passed as the local app storage directory, and append "cache/" to it
        wxString dataLoc = *GetpPrivateApplicationDataLocation();
        wxFileName fn(dataLoc);
        wxString dataDir = fn.GetPath(wxPATH_GET_SEPARATOR);
        dataDir += _T("cache/");

        wxString rootDir = fn.GetPath(wxPATH_GET_SEPARATOR);
        
        //  Set up the parameter passed to runtime environment as LD_LIBRARY_PATH
        // This will be {dir of g_sencutil_bin}/lib
        wxFileName fnl(cmd);
        wxString libDir = fnl.GetPath(wxPATH_GET_SEPARATOR) + _T("lib");
        
        wxLogMessage(_T("oeRNC_PI: Getting XFPR: Starting: ") + cmd );

        wxString result = callActivityMethod_s6s("createProcSync4", cmd, _T("-q"), rootDir, _T("-g"), dataDir, libDir);

        wxLogMessage(_T("oeRNC_PI: Start Result: ") + result);

        
        wxString sFPRPlus;              // The composite string we will pass to the management activity
        
        // Convert the XFPR to an ASCII string for transmission inter-process...
        // Find the file...
        wxArrayString files;
        wxString lastFile = _T("NOT_FOUND");
        time_t tmax = -1;
        size_t nf = wxDir::GetAllFiles(dataDir, &files, _T("*.fpr"), wxDIR_FILES);
        if(nf){
            for(size_t i = 0 ; i < files.GetCount() ; i++){
                qDebug() << "looking at FPR file: " << files[i].mb_str();
                time_t t = ::wxFileModificationTime(files[i]);
                if(t > tmax){
                    tmax = t;
                    lastFile = files[i];
                }
            }
        }
        
        qDebug() << "last FPR file: " << lastFile.mb_str();
            
        //Read the file, convert to ASCII hex, and build a string
        if(::wxFileExists(lastFile)){
            wxString stringFPR;
            wxFileInputStream stream(lastFile);
            while(stream.IsOk() && !stream.Eof() ){
                char c = stream.GetC();
                if(!stream.Eof()){
                    wxString sc;
                    sc.Printf(_T("%02X"), c);
                    stringFPR += sc;
                }
            }
            sFPRPlus += _T("FPR:");                 // name        
            sFPRPlus += stringFPR;                  // values
            sFPRPlus += _T(";");                    // delimiter
        }
        
        //  Add the filename
        wxFileName fnxpr(lastFile);
        wxString fprName = fnxpr.GetName();
        sFPRPlus += _T("fprName:");                 // name        
        sFPRPlus += fprName;                  // values
        sFPRPlus += _T(".fpr");
        sFPRPlus += _T(";");                    // delimiter
        

        // We can safely delete the FPR file now.
        if(::wxFileExists(lastFile))
            wxRemoveFile( lastFile );
        
        // Get and add other name/value pairs to the sFPRPlus string
        sFPRPlus += _T("User:");
        sFPRPlus += g_loginUser;
        sFPRPlus += _T(";");                    // delimiter
        
        sFPRPlus += _T("loginKey:");
        if(!g_loginKey.Length())
            sFPRPlus += _T("?");
        else
            sFPRPlus += g_loginKey;
        sFPRPlus += _T(";");                    // delimiter
        
        //  System Name
        sFPRPlus += _T("systemName:");
        sFPRPlus += g_systemName;
        sFPRPlus += _T(";");                    // delimiter
        
        //  ADMIN mode bit
        sFPRPlus += _T("ADMIN:");
        sFPRPlus += g_admin ? _T("1"):_T("0");
        sFPRPlus += _T(";");                    // delimiter
        
        qDebug() << "sFPRPlus: " << sFPRPlus.mb_str();
        
        m_eventTimer.Stop();
            
        wxLogMessage(_T("sFPRPlus: ") + sFPRPlus);
        
        // Start the Chart management activity
        callActivityMethod_s5s( "startActivityWithIntent", _T("org.opencpn.oesencplugin"), _T("ChartsetListActivity"), _T("FPRPlus"), sFPRPlus, _T("ManageResult") );
        
        // Start a timer to poll for results.
        m_timerAction = ACTION_ARB_RESULT_POLL;
        m_eventTimer.Start(1000, wxTIMER_CONTINUOUS);
        
        
#endif
        
}


void oernc_pi_event_handler::OnManageShopClick( wxCommandEvent &event )
{
    
#ifndef __OCPN__ANDROID__

        doShop();
#else

        // Get XFPR from the oeserverda helper utility.
        //  The target binary executable
        wxString cmd = g_sencutil_bin;

//  Set up the parameter passed as the local app storage directory, and append "cache/" to it
        wxString dataLoc = *GetpPrivateApplicationDataLocation();
        wxFileName fn(dataLoc);
        wxString dataDir = fn.GetPath(wxPATH_GET_SEPARATOR);
        dataDir += _T("cache/");

        wxString rootDir = fn.GetPath(wxPATH_GET_SEPARATOR);
        
        //  Set up the parameter passed to runtime environment as LD_LIBRARY_PATH
        // This will be {dir of g_sencutil_bin}/lib
        wxFileName fnl(cmd);
        wxString libDir = fnl.GetPath(wxPATH_GET_SEPARATOR) + _T("lib");
        
        wxLogMessage(_T("oeRNC_PI: Getting XFPR: Starting: ") + cmd );

        wxString result = callActivityMethod_s6s("createProcSync4", cmd, _T("-q"), rootDir, _T("-g"), dataDir, libDir);

        wxLogMessage(_T("oeRNC_PI: Start Result: ") + result);

        
        wxString sFPRPlus;              // The composite string we will pass to the management activity
        
        // Convert the XFPR to an ASCII string for transmission inter-process...
        // Find the file...
        wxArrayString files;
        wxString lastFile = _T("NOT_FOUND");
        time_t tmax = -1;
        size_t nf = wxDir::GetAllFiles(dataDir, &files, _T("*.fpr"), wxDIR_FILES);
        if(nf){
            for(size_t i = 0 ; i < files.GetCount() ; i++){
                qDebug() << "looking at FPR file: " << files[i].mb_str();
                time_t t = ::wxFileModificationTime(files[i]);
                if(t > tmax){
                    tmax = t;
                    lastFile = files[i];
                }
            }
        }
        
        qDebug() << "last FPR file: " << lastFile.mb_str();
            
        //Read the file, convert to ASCII hex, and build a string
        if(::wxFileExists(lastFile)){
            wxString stringFPR;
            wxFileInputStream stream(lastFile);
            while(stream.IsOk() && !stream.Eof() ){
                char c = stream.GetC();
                if(!stream.Eof()){
                    wxString sc;
                    sc.Printf(_T("%02X"), c);
                    stringFPR += sc;
                }
            }
            sFPRPlus += _T("FPR:");                 // name        
            sFPRPlus += stringFPR;                  // values
            sFPRPlus += _T(";");                    // delimiter
        }
        
        //  Add the filename
        wxFileName fnxpr(lastFile);
        wxString fprName = fnxpr.GetName();
        sFPRPlus += _T("fprName:");                 // name        
        sFPRPlus += fprName;                  // values
        sFPRPlus += _T(".fpr");
        sFPRPlus += _T(";");                    // delimiter
        

        // We can safely delete the FPR file now.
        if(::wxFileExists(lastFile))
            wxRemoveFile( lastFile );
        
        // Get and add other name/value pairs to the sFPRPlus string
        sFPRPlus += _T("User:");
        sFPRPlus += g_loginUser;
        sFPRPlus += _T(";");                    // delimiter
        
        sFPRPlus += _T("loginKey:");
        if(!g_loginKey.Length())
            sFPRPlus += _T("?");
        else
            sFPRPlus += g_loginKey;
        sFPRPlus += _T(";");                    // delimiter
        
        //  System Name
        sFPRPlus += _T("systemName:");
        sFPRPlus += g_systemName;
        sFPRPlus += _T(";");                    // delimiter
        
        //  ADMIN mode bit
        sFPRPlus += _T("ADMIN:");
        sFPRPlus += g_admin ? _T("1"):_T("0");
        sFPRPlus += _T(";");                    // delimiter
        
        qDebug() << "sFPRPlus: " << sFPRPlus.mb_str();
        
        m_eventTimer.Stop();
            
        wxLogMessage(_T("sFPRPlus: ") + sFPRPlus);
        
        // Start the Chart management activity
        callActivityMethod_s5s( "startActivityWithIntent", _T("org.opencpn.oesencplugin"), _T("ChartsetListActivity"), _T("FPRPlus"), sFPRPlus, _T("ManageResult") );
        
        // Start a timer to poll for results.
        m_timerAction = ACTION_ARB_RESULT_POLL;
        m_eventTimer.Start(1000, wxTIMER_CONTINUOUS);
        

#endif  // Android

    
}


void oernc_pi_event_handler::OnGetHWIDClick( wxCommandEvent &event )
{
#ifndef __OCPN__ANDROID__    

#else

        // Get XFPR from the oeserverda helper utility.
        //  The target binary executable
        wxString cmd = g_sencutil_bin;

//  Set up the parameter passed as the local app storage directory, and append "cache/" to it
        wxString dataLoc = *GetpPrivateApplicationDataLocation();
        wxFileName fn(dataLoc);
        wxString dataDir = fn.GetPath(wxPATH_GET_SEPARATOR);
        
        wxString rootDir = fn.GetPath(wxPATH_GET_SEPARATOR);
        
        //  Set up the parameter passed to runtime environment as LD_LIBRARY_PATH
        // This will be {dir of g_sencutil_bin}/lib
        wxFileName fnl(cmd);
        wxString libDir = fnl.GetPath(wxPATH_GET_SEPARATOR) + _T("lib");
        
        wxLogMessage(_T("oeRNC_PI: Getting HWID: Starting: ") + cmd );

        wxString result = callActivityMethod_s6s("createProcSync4", cmd, _T("-q"), rootDir, _T("-w"), dataDir, libDir);

        wxLogMessage(_T("oeRNC_PI: Start Result: ") + result);

#endif
        
}


#if 0
#ifdef __OCPN__ANDROID__

extern JavaVM *java_vm;         // found in androidUtil.cpp, accidentally exported....

bool CheckPendingJNIException()
{
    JNIEnv* jenv;
    
    if (java_vm->GetEnv( (void **) &jenv, JNI_VERSION_1_6) != JNI_OK) 
        return true;
    
    if( (jenv)->ExceptionCheck() == JNI_TRUE ) {
        
        // Handle exception here.
        (jenv)->ExceptionDescribe(); // writes to logcat
        (jenv)->ExceptionClear();
        
        return false;           // There was a pending exception, but cleared OK
        // interesting discussion:  http://blog.httrack.com/blog/2013/08/23/catching-posix-signals-on-android/
    }
    
    return false;
    
}


wxString callActivityMethod_s4s(const char *method, wxString parm1, wxString parm2, wxString parm3, wxString parm4)
{
    if(CheckPendingJNIException())
        return _T("NOK");
    JNIEnv* jenv;
    
    wxString return_string;
    QAndroidJniObject activity = QAndroidJniObject::callStaticObjectMethod("org/qtproject/qt5/android/QtNative",
                                                                           "activity", "()Landroid/app/Activity;");
    if(CheckPendingJNIException())
        return _T("NOK");
    
    if ( !activity.isValid() ){
        return return_string;
    }
    
    //  Need a Java environment to decode the resulting string
    if (java_vm->GetEnv( (void **) &jenv, JNI_VERSION_1_6) != JNI_OK) {
        return _T("jenv Error");
    }
    
    wxCharBuffer p1b = parm1.ToUTF8();
    jstring p1 = (jenv)->NewStringUTF(p1b.data());
    
    wxCharBuffer p2b = parm2.ToUTF8();
    jstring p2 = (jenv)->NewStringUTF(p2b.data());
    
    wxCharBuffer p3b = parm3.ToUTF8();
    jstring p3 = (jenv)->NewStringUTF(p3b.data());
    
    wxCharBuffer p4b = parm4.ToUTF8();
    jstring p4 = (jenv)->NewStringUTF(p4b.data());
    
    QAndroidJniObject data = activity.callObjectMethod(method, "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                                                       p1, p2, p3, p4);
    (jenv)->DeleteLocalRef(p1);
    (jenv)->DeleteLocalRef(p2);
    (jenv)->DeleteLocalRef(p3);
    (jenv)->DeleteLocalRef(p4);
    
    if(CheckPendingJNIException())
        return _T("NOK");
    
    //qDebug() << "Back from method_s4s";
        
        jstring s = data.object<jstring>();
        
        if( (jenv)->GetStringLength( s )){
            const char *ret_string = (jenv)->GetStringUTFChars(s, NULL);
            return_string = wxString(ret_string, wxConvUTF8);
        }
        
        return return_string;
        
}

#endif
#endif