/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/*!
 * @file StereoscopicsManager.cpp
 * @brief This class acts as container for stereoscopic related functions
 */

#include <stdlib.h>
#include "StereoscopicsManager.h"

#include "Application.h"
#include "ApplicationMessenger.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "dialogs/GUIDialogSelect.h"
#include "FileItem.h"
#include "GUIInfoManager.h"
#include "GUIUserMessages.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/Key.h"
#include "guilib/GUIWindowManager.h"
#include "settings/AdvancedSettings.h"
#include "settings/lib/ISettingCallback.h"
#include "settings/lib/Setting.h"
#include "settings/Settings.h"
#include "settings/VideoSettings.h"
#include "rendering/RenderSystem.h"
#include "utils/log.h"
#include "utils/RegExp.h"
#include "utils/StringUtils.h"
#include "URL.h"
#include "video/VideoInfoTag.h"
#include "video/VideoDatabase.h"
#include "windowing/WindowingFactory.h"


struct StereoModeMap
{
  const char*          name;
  RENDER_STEREO_MODE   mode;
};

struct StrStereoModeMap
{
  const char*   mode1;
  const char*   mode2;
};

static const struct StereoModeMap VideoModeToGuiModeMap[] =
{
  { "mono",                     RENDER_STEREO_MODE_OFF },
  { "left_right",               RENDER_STEREO_MODE_SPLIT_VERTICAL },
  { "right_left",               RENDER_STEREO_MODE_SPLIT_VERTICAL },
  { "top_bottom",               RENDER_STEREO_MODE_SPLIT_HORIZONTAL },
  { "bottom_top",               RENDER_STEREO_MODE_SPLIT_HORIZONTAL },
  { "checkerboard_rl",          RENDER_STEREO_MODE_OFF }, // unsupported
  { "checkerboard_lr",          RENDER_STEREO_MODE_OFF }, // unsupported
  { "row_interleaved_rl",       RENDER_STEREO_MODE_INTERLACED },
  { "row_interleaved_lr",       RENDER_STEREO_MODE_INTERLACED },
  { "col_interleaved_rl",       RENDER_STEREO_MODE_OFF }, // unsupported
  { "col_interleaved_lr",       RENDER_STEREO_MODE_OFF }, // unsupported
  { "anaglyph_cyan_red",        RENDER_STEREO_MODE_ANAGLYPH_RED_CYAN },
  { "anaglyph_green_magenta",   RENDER_STEREO_MODE_ANAGLYPH_GREEN_MAGENTA },
  { "block_lr",                 RENDER_STEREO_MODE_OFF }, // unsupported
  { "block_rl",                 RENDER_STEREO_MODE_OFF }, // unsupported
  {}
};

static const struct StereoModeMap StringToGuiModeMap[] =
{
  { "off",                      RENDER_STEREO_MODE_OFF },
  { "split_vertical",           RENDER_STEREO_MODE_SPLIT_VERTICAL },
  { "side_by_side",             RENDER_STEREO_MODE_SPLIT_VERTICAL }, // alias
  { "sbs",                      RENDER_STEREO_MODE_SPLIT_VERTICAL }, // alias
  { "split_horizontal",         RENDER_STEREO_MODE_SPLIT_HORIZONTAL },
  { "over_under",               RENDER_STEREO_MODE_SPLIT_HORIZONTAL }, // alias
  { "tab",                      RENDER_STEREO_MODE_SPLIT_HORIZONTAL }, // alias
  { "row_interleaved",          RENDER_STEREO_MODE_INTERLACED },
  { "interlaced",               RENDER_STEREO_MODE_INTERLACED }, // alias
  { "anaglyph_cyan_red",        RENDER_STEREO_MODE_ANAGLYPH_RED_CYAN },
  { "anaglyph_green_magenta",   RENDER_STEREO_MODE_ANAGLYPH_GREEN_MAGENTA },
  { "hardware_based",           RENDER_STEREO_MODE_HARDWAREBASED },
  { "monoscopic",               RENDER_STEREO_MODE_MONO },
  {}
};

static const struct StrStereoModeMap StereoModeInvertMap[] =
{
  { "left_right",               "right_left" },
  { "right_left",               "left_right" },
  { "bottom_top",               "top_bottom" },
  { "top_bottom",               "bottom_top" },
  { "checkerboard_rl",          "checkerboard_lr" },
  { "checkerboard_lr",          "checkerboard_rl" },
  { "row_interleaved_rl",       "row_interleaved_lr" },
  { "row_interleaved_lr",       "row_interleaved_rl" },
  { "col_interleaved_rl",       "col_interleaved_lr" },
  { "col_interleaved_lr",       "col_interleaved_rl" },
  { "block_lr",                 "block_lr" },
  { "block_rl",                 "block_rl" },
  {}
};

CStereoscopicsManager::CStereoscopicsManager(void)
{
  m_lastStereoMode = RENDER_STEREO_MODE_OFF;
}

CStereoscopicsManager::~CStereoscopicsManager(void)
{
}

CStereoscopicsManager& CStereoscopicsManager::Get(void)
{
  static CStereoscopicsManager sStereoscopicsManager;
  return sStereoscopicsManager;
}

void CStereoscopicsManager::Initialize(void)
{
  m_lastStereoMode = GetStereoMode();
  // turn off stereo mode on XBMC startup
  SetStereoMode(RENDER_STEREO_MODE_OFF);
}

RENDER_STEREO_MODE CStereoscopicsManager::GetStereoMode(void)
{
  return (RENDER_STEREO_MODE) CSettings::Get().GetInt("videoscreen.stereoscopicmode");
}

void CStereoscopicsManager::SetStereoMode(const RENDER_STEREO_MODE &mode)
{
  RENDER_STEREO_MODE currentMode = GetStereoMode();
  if (mode != currentMode && mode >= RENDER_STEREO_MODE_OFF)
  {
    if(!g_Windowing.SupportsStereo(mode))
      return;

    m_lastStereoMode = currentMode;
    CSettings::Get().SetInt("videoscreen.stereoscopicmode", mode);
  }
}

std::string CStereoscopicsManager::GetItemStereoMode(const std::string &itemPath)
{
  CFileItem item = CFileItem(itemPath, false);
  return GetItemStereoMode(item);
}

std::string CStereoscopicsManager::GetItemStereoMode(const CFileItem &item)
{
  std::string stereoMode;
  std::string path = item.GetPath();

  if (item.IsVideoDb() && item.HasVideoInfoTag())
    path = item.GetVideoInfoTag()->GetPath();

  // check for custom stereomode setting in video settings
  CVideoSettings itemVideoSettings;
  CVideoDatabase db;
  db.Open();
  if (db.GetVideoSettings(path, itemVideoSettings) && itemVideoSettings.m_StereoMode != RENDER_STEREO_MODE_OFF)
  {
    if (itemVideoSettings.m_StereoMode == RENDER_STEREO_MODE_SPLIT_HORIZONTAL)
      stereoMode = "left_right";
    else if (itemVideoSettings.m_StereoMode == RENDER_STEREO_MODE_SPLIT_VERTICAL)
      stereoMode = "top_bottom";
  }
  db.Close();

  // check stream details
  if (stereoMode.empty() && item.HasVideoInfoTag() && item.GetVideoInfoTag()->HasStreamDetails())
    stereoMode = item.GetVideoInfoTag()->m_streamDetails.GetStereoMode();

  // still empty, try grabbing from filename
  // TODO: in case of too many false positives due to using the full path, extract the filename only using string utils
  if (stereoMode.empty())
    stereoMode = DetectStereoModeByString( path );

  // still empty? Assume it's not stereoscopic
  if (stereoMode.empty())
    stereoMode = "mono";

  return stereoMode;
}

void CStereoscopicsManager::SetItemStereoMode(const std::string &itemPath, const std::string &mode)
{
  CFileItem item = CFileItem(itemPath, false);
  SetItemStereoMode(item, mode);
}

void CStereoscopicsManager::SetItemStereoMode(CFileItem &item, const std::string &mode)
{
  if (!item.HasVideoInfoTag() || !item.GetVideoInfoTag()->HasStreamDetails())
    return;

  std::string itemPath = item.GetVideoInfoTag()->GetPath();
  item.GetVideoInfoTag()->m_streamDetails.SetStereoMode(0, mode);

  CVideoDatabase db;
  db.Open();
  db.SetStreamDetailsForFile(item.GetVideoInfoTag()->m_streamDetails, itemPath);
  db.Close();
}

RENDER_STEREO_MODE CStereoscopicsManager::GetNextSupportedStereoMode(const RENDER_STEREO_MODE &currentMode, int step)
{
  RENDER_STEREO_MODE mode = currentMode;
  do {
    mode = (RENDER_STEREO_MODE) ((mode + step) % RENDER_STEREO_MODE_COUNT);
    if(g_Windowing.SupportsStereo(mode))
      break;
   } while (mode != currentMode);
  return mode;
}

std::string CStereoscopicsManager::DetectStereoModeByString(const std::string &needle)
{
  std::string stereoMode = "mono";
  CStdString searchString(needle);
  CRegExp re(true);

  if (!re.RegComp(g_advancedSettings.m_stereoscopicregex_3d.c_str()))
  {
    CLog::Log(LOGERROR, "%s: Invalid RegExp for matching 3d content:'%s'", __FUNCTION__, g_advancedSettings.m_stereoscopicregex_3d.c_str());
    return stereoMode;
  }

  if (re.RegFind(searchString) == -1)
    return stereoMode;    // no match found for 3d content, assume mono mode

  if (!re.RegComp(g_advancedSettings.m_stereoscopicregex_sbs.c_str()))
  {
    CLog::Log(LOGERROR, "%s: Invalid RegExp for matching 3d SBS content:'%s'", __FUNCTION__, g_advancedSettings.m_stereoscopicregex_sbs.c_str());
    return stereoMode;
  }

  if (re.RegFind(searchString) > -1)
  {
    stereoMode = "left_right";
    return stereoMode;
  }

  if (!re.RegComp(g_advancedSettings.m_stereoscopicregex_tab.c_str()))
  {
    CLog::Log(LOGERROR, "%s: Invalid RegExp for matching 3d TAB content:'%s'", __FUNCTION__, g_advancedSettings.m_stereoscopicregex_tab.c_str());
    return stereoMode;
  }

  if (re.RegFind(searchString) > -1)
    stereoMode = "top_bottom";

  return stereoMode;
}

RENDER_STEREO_MODE CStereoscopicsManager::GetStereoModeByUserChoice(const CStdString &heading)
{
  RENDER_STEREO_MODE mode = GetStereoMode();
  // if no stereo mode is set already, suggest mode of current video by preselecting it
  if (mode == RENDER_STEREO_MODE_OFF && g_infoManager.EvaluateBool("videoplayer.isstereoscopic"))
    mode = GetGuiStereoModeForPlayingVideo();

  CGUIDialogSelect* pDlgSelect = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
  pDlgSelect->Reset();
  if (heading.empty())
    pDlgSelect->SetHeading(g_localizeStrings.Get(36528).c_str());
  else
    pDlgSelect->SetHeading(heading.c_str());

  // prepare selectable stereo modes
  std::vector<RENDER_STEREO_MODE> selectableModes;
  for (int i = RENDER_STEREO_MODE_OFF; i < RENDER_STEREO_MODE_COUNT; i++)
  {
    RENDER_STEREO_MODE selectableMode = (RENDER_STEREO_MODE) i;
    if (g_Windowing.SupportsStereo(selectableMode))
    {
      selectableModes.push_back(selectableMode);
      CStdString label = g_localizeStrings.Get(36502+i);
      pDlgSelect->Add( label );
      if (mode == selectableMode)
        pDlgSelect->SetSelected( label );
    }
  }

  pDlgSelect->DoModal();

  int iItem = pDlgSelect->GetSelectedLabel();
  if (iItem > -1 && pDlgSelect->IsConfirmed())
    mode = (RENDER_STEREO_MODE) selectableModes[iItem];
  else
    mode = GetStereoMode();

  return mode;
}

std::string CStereoscopicsManager::GetStereoModeForPlayingVideo()
{
  std::string videoMode;

  CFileItem currentItem(g_application.CurrentFileItem());
  if (currentItem.HasVideoInfoTag())
    videoMode = GetItemStereoMode(currentItem);

  return videoMode;
}

RENDER_STEREO_MODE CStereoscopicsManager::GetGuiStereoModeForPlayingVideo()
{
  RENDER_STEREO_MODE mode = RENDER_STEREO_MODE_OFF;
  std::string videoMode = GetStereoModeForPlayingVideo();

  if (!videoMode.empty())
  {
    int convertedMode = ConvertVideoToGuiStereoMode(videoMode);
    if (convertedMode > -1)
      mode = (RENDER_STEREO_MODE) convertedMode;
    CLog::Log(LOGDEBUG, "StereoscopicsManager: autodetected GUI stereo mode for video mode %s is: %s", videoMode.c_str(), GetLabelForStereoMode(mode).c_str());
  }

  return mode;
}

CStdString CStereoscopicsManager::GetLabelForStereoMode(const RENDER_STEREO_MODE &mode)
{
  return g_localizeStrings.Get(36502 + mode);
}

RENDER_STEREO_MODE CStereoscopicsManager::GetPreferredPlaybackMode(void)
{
  RENDER_STEREO_MODE playbackMode = m_lastStereoMode;
  int preferredMode = CSettings::Get().GetInt("videoscreen.preferedstereoscopicmode");
  if (preferredMode == RENDER_STEREO_MODE_AUTO) // automatic mode, detect by movie
  {
    if (g_infoManager.EvaluateBool("videoplayer.isstereoscopic"))
      playbackMode = GetGuiStereoModeForPlayingVideo();
    else if (playbackMode == RENDER_STEREO_MODE_OFF)
      playbackMode = GetNextSupportedStereoMode(RENDER_STEREO_MODE_OFF);
  }
  else // predefined mode
  {
    playbackMode = (RENDER_STEREO_MODE) preferredMode;
  }
  return playbackMode;
}

std::string CStereoscopicsManager::GetStereoModeInverted(const std::string &mode)
{
  size_t i = 0;
  while (StereoModeInvertMap[i].mode1)
  {
    if (mode == StereoModeInvertMap[i].mode1)
      return StereoModeInvertMap[i].mode2;
    i++;
  }
  return mode;
}

int CStereoscopicsManager::ConvertVideoToGuiStereoMode(const std::string &mode)
{
  size_t i = 0;
  while (VideoModeToGuiModeMap[i].name)
  {
    if (mode == VideoModeToGuiModeMap[i].name)
      return VideoModeToGuiModeMap[i].mode;
    i++;
  }
  return -1;
}

int CStereoscopicsManager::ConvertStringToGuiStereoMode(const std::string &mode)
{
  size_t i = 0;
  while (StringToGuiModeMap[i].name)
  {
    if (mode == StringToGuiModeMap[i].name)
      return StringToGuiModeMap[i].mode;
    i++;
  }
  return ConvertVideoToGuiStereoMode(mode);
}

const char* CStereoscopicsManager::ConvertGuiStereoModeToString(const RENDER_STEREO_MODE &mode)
{
  size_t i = 0;
  while (StringToGuiModeMap[i].name)
  {
    if (StringToGuiModeMap[i].mode == mode)
      return StringToGuiModeMap[i].name;
    i++;
  }
  return "";
}

std::string CStereoscopicsManager::NormalizeStereoMode(const std::string &mode)
{
  if (!mode.empty() && mode != "mono")
  {
    int guiMode = ConvertStringToGuiStereoMode(mode);
    if (guiMode > -1)
      return ConvertGuiStereoModeToString((RENDER_STEREO_MODE) guiMode);
    else
      return mode;
  }
  return "mono";
}

CAction CStereoscopicsManager::ConvertActionCommandToAction(const std::string &command, const std::string &parameter)
{
  if (command == "SetStereoMode")
  {
    int actionId = -1;
    if (parameter == "next")
      actionId = ACTION_STEREOMODE_NEXT;
    else if (parameter == "previous")
      actionId = ACTION_STEREOMODE_PREVIOUS;
    else if (parameter == "toggle")
      actionId = ACTION_STEREOMODE_TOGGLE;
    else if (parameter == "select")
      actionId = ACTION_STEREOMODE_SELECT;
    else if (parameter == "tomono")
      actionId = ACTION_STEREOMODE_TOMONO;

    // already have a valid actionID return it
    if (actionId > -1)
      return CAction(actionId);

    // still no valid action ID, check if parameter is a supported stereomode
    if (ConvertStringToGuiStereoMode(parameter) > -1)
      return CAction(ACTION_STEREOMODE_SET, parameter);
  }
  return CAction(ACTION_NONE);
}

void CStereoscopicsManager::OnSettingChanged(const CSetting *setting)
{
  if (setting == NULL)
    return;

  const std::string &settingId = setting->GetId();

  if (settingId == "videoscreen.stereoscopicmode")
  {
    RENDER_STEREO_MODE mode = GetStereoMode();
    CLog::Log(LOGDEBUG, "StereoscopicsManager: stereo mode setting changed to %s", GetLabelForStereoMode(mode).c_str());
    ApplyStereoMode(mode);
  }
}

bool CStereoscopicsManager::OnMessage(CGUIMessage &message)
{
  switch (message.GetMessage())
  {
  case GUI_MSG_PLAYBACK_STARTED:
  case GUI_MSG_PLAYLISTPLAYER_STARTED:
  case GUI_MSG_PLAYLISTPLAYER_CHANGED:
    OnPlaybackStarted();
    break;
  case GUI_MSG_PLAYBACK_STOPPED:
  case GUI_MSG_PLAYLISTPLAYER_STOPPED:
    OnPlaybackStopped();
    break;
  }

  return false;
}

bool CStereoscopicsManager::OnAction(const CAction &action)
{
  RENDER_STEREO_MODE mode = GetStereoMode();

  if (action.GetID() == ACTION_STEREOMODE_NEXT)
  {
    SetStereoMode(GetNextSupportedStereoMode(mode));
    return true;
  }
  else if (action.GetID() == ACTION_STEREOMODE_PREVIOUS)
  {
    SetStereoMode(GetNextSupportedStereoMode(mode, RENDER_STEREO_MODE_COUNT - 1));
    return true;
  }
  else if (action.GetID() == ACTION_STEREOMODE_TOGGLE)
  {
    if (mode == RENDER_STEREO_MODE_OFF)
    {
      RENDER_STEREO_MODE targetMode = m_lastStereoMode;
      if (targetMode == RENDER_STEREO_MODE_OFF)
        targetMode = GetPreferredPlaybackMode();
      SetStereoMode(targetMode);
    }
    else
    {
      SetStereoMode(RENDER_STEREO_MODE_OFF);
    }
    return true;
  }
  else if (action.GetID() == ACTION_STEREOMODE_SELECT)
  {
    SetStereoMode(GetStereoModeByUserChoice());
    return true;
  }
  else if (action.GetID() == ACTION_STEREOMODE_TOMONO)
  {
    if (mode == RENDER_STEREO_MODE_MONO)
    {
      RENDER_STEREO_MODE targetMode = m_lastStereoMode;
      if (targetMode == RENDER_STEREO_MODE_OFF)
        targetMode = GetPreferredPlaybackMode();
      SetStereoMode(targetMode);
    }
    else
    {
      SetStereoMode(RENDER_STEREO_MODE_MONO);
    }
  }
  else if (action.GetID() == ACTION_STEREOMODE_SET)
  {
    int stereoMode = ConvertStringToGuiStereoMode(action.GetName());
    if (stereoMode > -1)
      SetStereoMode( (RENDER_STEREO_MODE) stereoMode);
  }

  return false;
}

void CStereoscopicsManager::ApplyStereoMode(const RENDER_STEREO_MODE &mode, bool notify)
{
  RENDER_STEREO_MODE currentMode = g_graphicsContext.GetStereoMode();
  CLog::Log(LOGDEBUG, "StereoscopicsManager::ApplyStereoMode: trying to apply stereo mode. Current: %s | Target: %s", GetLabelForStereoMode(currentMode).c_str(), GetLabelForStereoMode(mode).c_str());
  if (currentMode != mode)
  {
    g_graphicsContext.SetStereoMode(mode);
    CLog::Log(LOGDEBUG, "StereoscopicsManager: stereo mode changed to %s", GetLabelForStereoMode(mode).c_str());
    if (notify)
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Info, g_localizeStrings.Get(36501), GetLabelForStereoMode(mode));
  }
}

void CStereoscopicsManager::OnPlaybackStarted(void)
{
  RENDER_STEREO_MODE mode = GetStereoMode();

  if (!g_infoManager.EvaluateBool("videoplayer.isstereoscopic"))
  {
    // exit stereo mode if started item is not stereoscopic but we're currently in a stereo mode
    if (mode != RENDER_STEREO_MODE_OFF)
      SetStereoMode(RENDER_STEREO_MODE_OFF);
    return;
  }

  // only change stereo mode if not yet in preferred stereo mode
  RENDER_STEREO_MODE preferred = GetPreferredPlaybackMode();
  if (mode != RENDER_STEREO_MODE_OFF && mode == preferred)
    return;

  int playbackMode = CSettings::Get().GetInt("videoplayer.stereoscopicplaybackmode");
  switch (playbackMode)
  {
  case 0: // Ask
    {
      CApplicationMessenger::Get().MediaPause();

      CGUIDialogSelect* pDlgSelect = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
      pDlgSelect->Reset();
      pDlgSelect->SetHeading(g_localizeStrings.Get(36527).c_str());

      RENDER_STEREO_MODE playing = GetGuiStereoModeForPlayingVideo();

      int idx_playing   = -1
        , idx_mono      = -1;
        

      // add choices
      int idx_preferred = pDlgSelect->Add( g_localizeStrings.Get(36530)
                                     + " ("
                                     + GetLabelForStereoMode(preferred)
                                     + ")");

      if(preferred != RENDER_STEREO_MODE_MONO)
        idx_mono = pDlgSelect->Add( g_localizeStrings.Get(36529) ); // mono / 2d


      if(playing != RENDER_STEREO_MODE_OFF && playing != preferred && g_Windowing.SupportsStereo(playing))
        idx_playing = pDlgSelect->Add( g_localizeStrings.Get(36532)
                                    + " ("
                                    + GetLabelForStereoMode(playing)
                                    + ")");

      int idx_select = pDlgSelect->Add( g_localizeStrings.Get(36531) ); // other / select

      pDlgSelect->DoModal();

      if(pDlgSelect->IsConfirmed())
      {
        int iItem = pDlgSelect->GetSelectedLabel();
        if      (iItem == idx_preferred) mode = preferred;
        else if (iItem == idx_mono)      mode = RENDER_STEREO_MODE_MONO;
        else if (iItem == idx_playing)   mode = playing;
        else if (iItem == idx_select)    mode = GetStereoModeByUserChoice();

        SetStereoMode(mode);
      }

      CApplicationMessenger::Get().MediaUnPause();
    }
    break;
  case 1: // Stereoscopic
    SetStereoMode( preferred );
    break;
  default:
    break;
  }
}

void CStereoscopicsManager::OnPlaybackStopped(void)
{
  RENDER_STEREO_MODE mode = GetStereoMode();
  if (CSettings::Get().GetBool("videoplayer.quitstereomodeonstop") == true && mode != RENDER_STEREO_MODE_OFF)
  {
    SetStereoMode(RENDER_STEREO_MODE_OFF);
  }
}
