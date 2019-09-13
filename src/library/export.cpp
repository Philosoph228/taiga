/*
** Taiga
** Copyright (C) 2010-2019, Eren Okka
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <map>

#include "base/file.h"
#include "base/format.h"
#include "base/string.h"
#include "base/time.h"
#include "base/xml.h"
#include "library/anime_db.h"
#include "library/anime_item.h"
#include "library/anime_util.h"
#include "library/export.h"
#include "library/history.h"
#include "sync/myanimelist_types.h"
#include "sync/myanimelist_util.h"
#include "taiga/settings.h"
#include "taiga/version.h"
#include "ui/translate.h"

namespace library {

bool ExportAsMalXml(const std::wstring& path) {
  constexpr auto count_total_anime = []() {
    int count = 0;
    for (const auto& [id, item] : AnimeDatabase.items) {
      if (item.IsInList()) {
        count += 1;
      }
    }
    return count;
  };

  constexpr auto tr_series_type = [](int type) {
    switch (sync::myanimelist::TranslateSeriesTypeTo(type)) {
      default:
      case sync::myanimelist::kUnknownType: return L"Unknown";
      case sync::myanimelist::kTv: return L"TV";
      case sync::myanimelist::kOva: return L"OVA";
      case sync::myanimelist::kMovie: return L"Movie";
      case sync::myanimelist::kSpecial: return L"Special";
      case sync::myanimelist::kOna: return L"ONA";
      case sync::myanimelist::kMusic: return L"Music";
    }
  };

  constexpr auto tr_my_status = [](int status) {
    switch (sync::myanimelist::TranslateMyStatusTo(status)) {
      default:
      case sync::myanimelist::kWatching: return L"Watching";
      case sync::myanimelist::kCompleted: return L"Completed";
      case sync::myanimelist::kOnHold: return L"On-Hold";
      case sync::myanimelist::kDropped: return L"Dropped";
      case sync::myanimelist::kPlanToWatch: return L"Plan to Watch";
    }
  };

  XmlDocument document;

  auto node_decl = document.prepend_child(pugi::node_declaration);
  node_decl.append_attribute(L"version") = L"1.0";
  node_decl.append_attribute(L"encoding") = L"UTF-8";

  auto node_comment = document.append_child(pugi::node_comment);
  node_comment.set_value(L" Generated by Taiga v{} on {} {} "_format(
      StrToWstr(taiga::version().to_string()),
      GetDate().to_string(),
      GetTime()).c_str());

  auto node_myanimelist = document.append_child(L"myanimelist");

  auto node_myinfo = node_myanimelist.append_child(L"myinfo");
  XmlWriteInt(node_myinfo, L"user_id", 0);
  XmlWriteStr(node_myinfo, L"user_name", taiga::GetCurrentUsername());
  XmlWriteInt(node_myinfo, L"user_export_type", 1);  // anime
  XmlWriteInt(node_myinfo, L"user_total_anime", count_total_anime());
  XmlWriteInt(node_myinfo, L"user_total_watching", AnimeDatabase.GetItemCount(anime::kWatching));
  XmlWriteInt(node_myinfo, L"user_total_completed", AnimeDatabase.GetItemCount(anime::kCompleted));
  XmlWriteInt(node_myinfo, L"user_total_onhold", AnimeDatabase.GetItemCount(anime::kOnHold));
  XmlWriteInt(node_myinfo, L"user_total_dropped", AnimeDatabase.GetItemCount(anime::kDropped));
  XmlWriteInt(node_myinfo, L"user_total_plantowatch", AnimeDatabase.GetItemCount(anime::kPlanToWatch));

  for (const auto& [id, item] : AnimeDatabase.items) {
    if (item.IsInList()) {
      auto node = node_myanimelist.append_child(L"anime");
      XmlWriteInt(node, L"series_animedb_id", item.GetId());
      XmlWriteStr(node, L"series_title", item.GetTitle(), pugi::node_cdata);
      XmlWriteStr(node, L"series_type", tr_series_type(item.GetType()));
      XmlWriteInt(node, L"series_episodes", item.GetEpisodeCount());

      XmlWriteInt(node, L"my_id", 0);
      XmlWriteInt(node, L"my_watched_episodes", item.GetMyLastWatchedEpisode());
      XmlWriteStr(node, L"my_start_date", item.GetMyDateStart().to_string());
      XmlWriteStr(node, L"my_finish_date", item.GetMyDateEnd().to_string());
      XmlWriteStr(node, L"my_fansub_group", L"", pugi::node_cdata);
      XmlWriteStr(node, L"my_rated", L"");
      XmlWriteInt(node, L"my_score", sync::myanimelist::TranslateMyRatingTo(item.GetMyScore()));
      XmlWriteStr(node, L"my_dvd", L"");
      XmlWriteStr(node, L"my_storage", L"");
      XmlWriteStr(node, L"my_status", tr_my_status(item.GetMyStatus()));
      XmlWriteStr(node, L"my_comments", item.GetMyNotes(), pugi::node_cdata);
      XmlWriteInt(node, L"my_times_watched", item.GetMyRewatchedTimes());
      XmlWriteStr(node, L"my_rewatch_value", L"");
      XmlWriteInt(node, L"my_downloaded_eps", 0);
      XmlWriteStr(node, L"my_tags", item.GetMyTags(), pugi::node_cdata);
      XmlWriteInt(node, L"my_rewatching", item.GetMyRewatching());
      XmlWriteInt(node, L"my_rewatching_ep", item.GetMyRewatchingEp());
      XmlWriteInt(node, L"update_on_import", History.queue.IsQueued(item.GetId()));
    }
  }

  return XmlSaveDocumentToFile(document, path);
}

bool ExportAsMarkdown(const std::wstring& path) {
  std::map<int, std::vector<std::wstring>> status_lists;

  for (const auto& [id, item] : AnimeDatabase.items) {
    if (item.IsInList()) {
      status_lists[item.GetMyStatus()].push_back(L"{} ({}/{})"_format(
          anime::GetPreferredTitle(item),
          item.GetMyLastWatchedEpisode(),
          ui::TranslateNumber(item.GetEpisodeCount(), L"?")));
    }
  }

  for (auto& [status, list] : status_lists) {
    std::sort(list.begin(), list.end(),
              [](const std::wstring& a, const std::wstring& b) {
                return CompareStrings(a, b, true) < 0;
              });
  }

  std::wstring text;
  for (const auto& [status, list] : status_lists) {
    if (!text.empty())
      text += L"\r\n";
    text += L"# {}\r\n\r\n"_format(ui::TranslateMyStatus(status, true));
    for (const auto& line : list) {
      text += L"- {}\r\n"_format(line);
    }
  }

  return SaveToFile(WstrToStr(text), path, false);
}

}  // namespace library
