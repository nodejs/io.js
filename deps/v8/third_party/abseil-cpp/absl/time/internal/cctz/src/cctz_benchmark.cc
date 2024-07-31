// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   https://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.

#include <algorithm>
#include <cassert>
#include <chrono>
#include <ctime>
#include <random>
#include <string>
#include <vector>

#include "benchmark/benchmark.h"
#include "absl/time/internal/cctz/include/cctz/civil_time.h"
#include "absl/time/internal/cctz/include/cctz/time_zone.h"
#include "time_zone_impl.h"

namespace {

namespace cctz = absl::time_internal::cctz;

void BM_Difference_Days(benchmark::State& state) {
  const cctz::civil_day c(2014, 8, 22);
  const cctz::civil_day epoch(1970, 1, 1);
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(c - epoch);
  }
}
BENCHMARK(BM_Difference_Days);

void BM_Step_Days(benchmark::State& state) {
  const cctz::civil_day kStart(2014, 8, 22);
  cctz::civil_day c = kStart;
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(++c);
  }
}
BENCHMARK(BM_Step_Days);

void BM_GetWeekday(benchmark::State& state) {
  const cctz::civil_day c(2014, 8, 22);
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(cctz::get_weekday(c));
  }
}
BENCHMARK(BM_GetWeekday);

void BM_NextWeekday(benchmark::State& state) {
  const cctz::civil_day kStart(2014, 8, 22);
  const cctz::civil_day kDays[7] = {
      kStart + 0, kStart + 1, kStart + 2, kStart + 3,
      kStart + 4, kStart + 5, kStart + 6,
  };
  const cctz::weekday kWeekdays[7] = {
      cctz::weekday::monday,   cctz::weekday::tuesday, cctz::weekday::wednesday,
      cctz::weekday::thursday, cctz::weekday::friday,  cctz::weekday::saturday,
      cctz::weekday::sunday,
  };
  while (state.KeepRunningBatch(7 * 7)) {
    for (const auto from : kDays) {
      for (const auto to : kWeekdays) {
        benchmark::DoNotOptimize(cctz::next_weekday(from, to));
      }
    }
  }
}
BENCHMARK(BM_NextWeekday);

void BM_PrevWeekday(benchmark::State& state) {
  const cctz::civil_day kStart(2014, 8, 22);
  const cctz::civil_day kDays[7] = {
      kStart + 0, kStart + 1, kStart + 2, kStart + 3,
      kStart + 4, kStart + 5, kStart + 6,
  };
  const cctz::weekday kWeekdays[7] = {
      cctz::weekday::monday,   cctz::weekday::tuesday, cctz::weekday::wednesday,
      cctz::weekday::thursday, cctz::weekday::friday,  cctz::weekday::saturday,
      cctz::weekday::sunday,
  };
  while (state.KeepRunningBatch(7 * 7)) {
    for (const auto from : kDays) {
      for (const auto to : kWeekdays) {
        benchmark::DoNotOptimize(cctz::prev_weekday(from, to));
      }
    }
  }
}
BENCHMARK(BM_PrevWeekday);

const char RFC3339_full[] = "%Y-%m-%d%ET%H:%M:%E*S%Ez";
const char RFC3339_sec[] = "%Y-%m-%d%ET%H:%M:%S%Ez";

const char RFC1123_full[] = "%a, %d %b %Y %H:%M:%S %z";
const char RFC1123_no_wday[] = "%d %b %Y %H:%M:%S %z";

// A list of known time-zone names.
// TODO: Refactor with src/time_zone_lookup_test.cc.
const char* const kTimeZoneNames[] = {"Africa/Abidjan",
                                      "Africa/Accra",
                                      "Africa/Addis_Ababa",
                                      "Africa/Algiers",
                                      "Africa/Asmara",
                                      "Africa/Bamako",
                                      "Africa/Bangui",
                                      "Africa/Banjul",
                                      "Africa/Bissau",
                                      "Africa/Blantyre",
                                      "Africa/Brazzaville",
                                      "Africa/Bujumbura",
                                      "Africa/Cairo",
                                      "Africa/Casablanca",
                                      "Africa/Ceuta",
                                      "Africa/Conakry",
                                      "Africa/Dakar",
                                      "Africa/Dar_es_Salaam",
                                      "Africa/Djibouti",
                                      "Africa/Douala",
                                      "Africa/El_Aaiun",
                                      "Africa/Freetown",
                                      "Africa/Gaborone",
                                      "Africa/Harare",
                                      "Africa/Johannesburg",
                                      "Africa/Juba",
                                      "Africa/Kampala",
                                      "Africa/Khartoum",
                                      "Africa/Kigali",
                                      "Africa/Kinshasa",
                                      "Africa/Lagos",
                                      "Africa/Libreville",
                                      "Africa/Lome",
                                      "Africa/Luanda",
                                      "Africa/Lubumbashi",
                                      "Africa/Lusaka",
                                      "Africa/Malabo",
                                      "Africa/Maputo",
                                      "Africa/Maseru",
                                      "Africa/Mbabane",
                                      "Africa/Mogadishu",
                                      "Africa/Monrovia",
                                      "Africa/Nairobi",
                                      "Africa/Ndjamena",
                                      "Africa/Niamey",
                                      "Africa/Nouakchott",
                                      "Africa/Ouagadougou",
                                      "Africa/Porto-Novo",
                                      "Africa/Sao_Tome",
                                      "Africa/Timbuktu",
                                      "Africa/Tripoli",
                                      "Africa/Tunis",
                                      "Africa/Windhoek",
                                      "America/Adak",
                                      "America/Anchorage",
                                      "America/Anguilla",
                                      "America/Antigua",
                                      "America/Araguaina",
                                      "America/Argentina/Buenos_Aires",
                                      "America/Argentina/Catamarca",
                                      "America/Argentina/Cordoba",
                                      "America/Argentina/Jujuy",
                                      "America/Argentina/La_Rioja",
                                      "America/Argentina/Mendoza",
                                      "America/Argentina/Rio_Gallegos",
                                      "America/Argentina/Salta",
                                      "America/Argentina/San_Juan",
                                      "America/Argentina/San_Luis",
                                      "America/Argentina/Tucuman",
                                      "America/Argentina/Ushuaia",
                                      "America/Aruba",
                                      "America/Asuncion",
                                      "America/Atikokan",
                                      "America/Atka",
                                      "America/Bahia",
                                      "America/Bahia_Banderas",
                                      "America/Barbados",
                                      "America/Belem",
                                      "America/Belize",
                                      "America/Blanc-Sablon",
                                      "America/Boa_Vista",
                                      "America/Bogota",
                                      "America/Boise",
                                      "America/Cambridge_Bay",
                                      "America/Campo_Grande",
                                      "America/Cancun",
                                      "America/Caracas",
                                      "America/Cayenne",
                                      "America/Cayman",
                                      "America/Chicago",
                                      "America/Chihuahua",
                                      "America/Ciudad_Juarez",
                                      "America/Coral_Harbour",
                                      "America/Costa_Rica",
                                      "America/Creston",
                                      "America/Cuiaba",
                                      "America/Curacao",
                                      "America/Danmarkshavn",
                                      "America/Dawson",
                                      "America/Dawson_Creek",
                                      "America/Denver",
                                      "America/Detroit",
                                      "America/Dominica",
                                      "America/Edmonton",
                                      "America/Eirunepe",
                                      "America/El_Salvador",
                                      "America/Ensenada",
                                      "America/Fort_Nelson",
                                      "America/Fortaleza",
                                      "America/Glace_Bay",
                                      "America/Godthab",
                                      "America/Goose_Bay",
                                      "America/Grand_Turk",
                                      "America/Grenada",
                                      "America/Guadeloupe",
                                      "America/Guatemala",
                                      "America/Guayaquil",
                                      "America/Guyana",
                                      "America/Halifax",
                                      "America/Havana",
                                      "America/Hermosillo",
                                      "America/Indiana/Indianapolis",
                                      "America/Indiana/Knox",
                                      "America/Indiana/Marengo",
                                      "America/Indiana/Petersburg",
                                      "America/Indiana/Tell_City",
                                      "America/Indiana/Vevay",
                                      "America/Indiana/Vincennes",
                                      "America/Indiana/Winamac",
                                      "America/Inuvik",
                                      "America/Iqaluit",
                                      "America/Jamaica",
                                      "America/Juneau",
                                      "America/Kentucky/Louisville",
                                      "America/Kentucky/Monticello",
                                      "America/Kralendijk",
                                      "America/La_Paz",
                                      "America/Lima",
                                      "America/Los_Angeles",
                                      "America/Lower_Princes",
                                      "America/Maceio",
                                      "America/Managua",
                                      "America/Manaus",
                                      "America/Marigot",
                                      "America/Martinique",
                                      "America/Matamoros",
                                      "America/Mazatlan",
                                      "America/Menominee",
                                      "America/Merida",
                                      "America/Metlakatla",
                                      "America/Mexico_City",
                                      "America/Miquelon",
                                      "America/Moncton",
                                      "America/Monterrey",
                                      "America/Montevideo",
                                      "America/Montreal",
                                      "America/Montserrat",
                                      "America/Nassau",
                                      "America/New_York",
                                      "America/Nipigon",
                                      "America/Nome",
                                      "America/Noronha",
                                      "America/North_Dakota/Beulah",
                                      "America/North_Dakota/Center",
                                      "America/North_Dakota/New_Salem",
                                      "America/Nuuk",
                                      "America/Ojinaga",
                                      "America/Panama",
                                      "America/Pangnirtung",
                                      "America/Paramaribo",
                                      "America/Phoenix",
                                      "America/Port-au-Prince",
                                      "America/Port_of_Spain",
                                      "America/Porto_Acre",
                                      "America/Porto_Velho",
                                      "America/Puerto_Rico",
                                      "America/Punta_Arenas",
                                      "America/Rainy_River",
                                      "America/Rankin_Inlet",
                                      "America/Recife",
                                      "America/Regina",
                                      "America/Resolute",
                                      "America/Rio_Branco",
                                      "America/Santa_Isabel",
                                      "America/Santarem",
                                      "America/Santiago",
                                      "America/Santo_Domingo",
                                      "America/Sao_Paulo",
                                      "America/Scoresbysund",
                                      "America/Shiprock",
                                      "America/Sitka",
                                      "America/St_Barthelemy",
                                      "America/St_Johns",
                                      "America/St_Kitts",
                                      "America/St_Lucia",
                                      "America/St_Thomas",
                                      "America/St_Vincent",
                                      "America/Swift_Current",
                                      "America/Tegucigalpa",
                                      "America/Thule",
                                      "America/Thunder_Bay",
                                      "America/Tijuana",
                                      "America/Toronto",
                                      "America/Tortola",
                                      "America/Vancouver",
                                      "America/Virgin",
                                      "America/Whitehorse",
                                      "America/Winnipeg",
                                      "America/Yakutat",
                                      "America/Yellowknife",
                                      "Antarctica/Casey",
                                      "Antarctica/Davis",
                                      "Antarctica/DumontDUrville",
                                      "Antarctica/Macquarie",
                                      "Antarctica/Mawson",
                                      "Antarctica/McMurdo",
                                      "Antarctica/Palmer",
                                      "Antarctica/Rothera",
                                      "Antarctica/Syowa",
                                      "Antarctica/Troll",
                                      "Antarctica/Vostok",
                                      "Arctic/Longyearbyen",
                                      "Asia/Aden",
                                      "Asia/Almaty",
                                      "Asia/Amman",
                                      "Asia/Anadyr",
                                      "Asia/Aqtau",
                                      "Asia/Aqtobe",
                                      "Asia/Ashgabat",
                                      "Asia/Atyrau",
                                      "Asia/Baghdad",
                                      "Asia/Bahrain",
                                      "Asia/Baku",
                                      "Asia/Bangkok",
                                      "Asia/Barnaul",
                                      "Asia/Beirut",
                                      "Asia/Bishkek",
                                      "Asia/Brunei",
                                      "Asia/Chita",
                                      "Asia/Choibalsan",
                                      "Asia/Chongqing",
                                      "Asia/Colombo",
                                      "Asia/Damascus",
                                      "Asia/Dhaka",
                                      "Asia/Dili",
                                      "Asia/Dubai",
                                      "Asia/Dushanbe",
                                      "Asia/Famagusta",
                                      "Asia/Gaza",
                                      "Asia/Harbin",
                                      "Asia/Hebron",
                                      "Asia/Ho_Chi_Minh",
                                      "Asia/Hong_Kong",
                                      "Asia/Hovd",
                                      "Asia/Irkutsk",
                                      "Asia/Istanbul",
                                      "Asia/Jakarta",
                                      "Asia/Jayapura",
                                      "Asia/Jerusalem",
                                      "Asia/Kabul",
                                      "Asia/Kamchatka",
                                      "Asia/Karachi",
                                      "Asia/Kashgar",
                                      "Asia/Kathmandu",
                                      "Asia/Khandyga",
                                      "Asia/Kolkata",
                                      "Asia/Krasnoyarsk",
                                      "Asia/Kuala_Lumpur",
                                      "Asia/Kuching",
                                      "Asia/Kuwait",
                                      "Asia/Macau",
                                      "Asia/Magadan",
                                      "Asia/Makassar",
                                      "Asia/Manila",
                                      "Asia/Muscat",
                                      "Asia/Nicosia",
                                      "Asia/Novokuznetsk",
                                      "Asia/Novosibirsk",
                                      "Asia/Omsk",
                                      "Asia/Oral",
                                      "Asia/Phnom_Penh",
                                      "Asia/Pontianak",
                                      "Asia/Pyongyang",
                                      "Asia/Qatar",
                                      "Asia/Qostanay",
                                      "Asia/Qyzylorda",
                                      "Asia/Riyadh",
                                      "Asia/Sakhalin",
                                      "Asia/Samarkand",
                                      "Asia/Seoul",
                                      "Asia/Shanghai",
                                      "Asia/Singapore",
                                      "Asia/Srednekolymsk",
                                      "Asia/Taipei",
                                      "Asia/Tashkent",
                                      "Asia/Tbilisi",
                                      "Asia/Tehran",
                                      "Asia/Tel_Aviv",
                                      "Asia/Thimphu",
                                      "Asia/Tokyo",
                                      "Asia/Tomsk",
                                      "Asia/Ulaanbaatar",
                                      "Asia/Urumqi",
                                      "Asia/Ust-Nera",
                                      "Asia/Vientiane",
                                      "Asia/Vladivostok",
                                      "Asia/Yakutsk",
                                      "Asia/Yangon",
                                      "Asia/Yekaterinburg",
                                      "Asia/Yerevan",
                                      "Atlantic/Azores",
                                      "Atlantic/Bermuda",
                                      "Atlantic/Canary",
                                      "Atlantic/Cape_Verde",
                                      "Atlantic/Faroe",
                                      "Atlantic/Jan_Mayen",
                                      "Atlantic/Madeira",
                                      "Atlantic/Reykjavik",
                                      "Atlantic/South_Georgia",
                                      "Atlantic/St_Helena",
                                      "Atlantic/Stanley",
                                      "Australia/Adelaide",
                                      "Australia/Brisbane",
                                      "Australia/Broken_Hill",
                                      "Australia/Canberra",
                                      "Australia/Currie",
                                      "Australia/Darwin",
                                      "Australia/Eucla",
                                      "Australia/Hobart",
                                      "Australia/Lindeman",
                                      "Australia/Lord_Howe",
                                      "Australia/Melbourne",
                                      "Australia/Perth",
                                      "Australia/Sydney",
                                      "Australia/Yancowinna",
                                      "Etc/GMT",
                                      "Etc/GMT+0",
                                      "Etc/GMT+1",
                                      "Etc/GMT+10",
                                      "Etc/GMT+11",
                                      "Etc/GMT+12",
                                      "Etc/GMT+2",
                                      "Etc/GMT+3",
                                      "Etc/GMT+4",
                                      "Etc/GMT+5",
                                      "Etc/GMT+6",
                                      "Etc/GMT+7",
                                      "Etc/GMT+8",
                                      "Etc/GMT+9",
                                      "Etc/GMT-0",
                                      "Etc/GMT-1",
                                      "Etc/GMT-10",
                                      "Etc/GMT-11",
                                      "Etc/GMT-12",
                                      "Etc/GMT-13",
                                      "Etc/GMT-14",
                                      "Etc/GMT-2",
                                      "Etc/GMT-3",
                                      "Etc/GMT-4",
                                      "Etc/GMT-5",
                                      "Etc/GMT-6",
                                      "Etc/GMT-7",
                                      "Etc/GMT-8",
                                      "Etc/GMT-9",
                                      "Etc/GMT0",
                                      "Etc/Greenwich",
                                      "Etc/UCT",
                                      "Etc/UTC",
                                      "Etc/Universal",
                                      "Etc/Zulu",
                                      "Europe/Amsterdam",
                                      "Europe/Andorra",
                                      "Europe/Astrakhan",
                                      "Europe/Athens",
                                      "Europe/Belfast",
                                      "Europe/Belgrade",
                                      "Europe/Berlin",
                                      "Europe/Bratislava",
                                      "Europe/Brussels",
                                      "Europe/Bucharest",
                                      "Europe/Budapest",
                                      "Europe/Busingen",
                                      "Europe/Chisinau",
                                      "Europe/Copenhagen",
                                      "Europe/Dublin",
                                      "Europe/Gibraltar",
                                      "Europe/Guernsey",
                                      "Europe/Helsinki",
                                      "Europe/Isle_of_Man",
                                      "Europe/Istanbul",
                                      "Europe/Jersey",
                                      "Europe/Kaliningrad",
                                      "Europe/Kirov",
                                      "Europe/Kyiv",
                                      "Europe/Lisbon",
                                      "Europe/Ljubljana",
                                      "Europe/London",
                                      "Europe/Luxembourg",
                                      "Europe/Madrid",
                                      "Europe/Malta",
                                      "Europe/Mariehamn",
                                      "Europe/Minsk",
                                      "Europe/Monaco",
                                      "Europe/Moscow",
                                      "Europe/Nicosia",
                                      "Europe/Oslo",
                                      "Europe/Paris",
                                      "Europe/Podgorica",
                                      "Europe/Prague",
                                      "Europe/Riga",
                                      "Europe/Rome",
                                      "Europe/Samara",
                                      "Europe/San_Marino",
                                      "Europe/Sarajevo",
                                      "Europe/Saratov",
                                      "Europe/Simferopol",
                                      "Europe/Skopje",
                                      "Europe/Sofia",
                                      "Europe/Stockholm",
                                      "Europe/Tallinn",
                                      "Europe/Tirane",
                                      "Europe/Tiraspol",
                                      "Europe/Ulyanovsk",
                                      "Europe/Vaduz",
                                      "Europe/Vatican",
                                      "Europe/Vienna",
                                      "Europe/Vilnius",
                                      "Europe/Volgograd",
                                      "Europe/Warsaw",
                                      "Europe/Zagreb",
                                      "Europe/Zurich",
                                      "Factory",
                                      "Indian/Antananarivo",
                                      "Indian/Chagos",
                                      "Indian/Christmas",
                                      "Indian/Cocos",
                                      "Indian/Comoro",
                                      "Indian/Kerguelen",
                                      "Indian/Mahe",
                                      "Indian/Maldives",
                                      "Indian/Mauritius",
                                      "Indian/Mayotte",
                                      "Indian/Reunion",
                                      "Pacific/Apia",
                                      "Pacific/Auckland",
                                      "Pacific/Bougainville",
                                      "Pacific/Chatham",
                                      "Pacific/Chuuk",
                                      "Pacific/Easter",
                                      "Pacific/Efate",
                                      "Pacific/Fakaofo",
                                      "Pacific/Fiji",
                                      "Pacific/Funafuti",
                                      "Pacific/Galapagos",
                                      "Pacific/Gambier",
                                      "Pacific/Guadalcanal",
                                      "Pacific/Guam",
                                      "Pacific/Honolulu",
                                      "Pacific/Johnston",
                                      "Pacific/Kanton",
                                      "Pacific/Kiritimati",
                                      "Pacific/Kosrae",
                                      "Pacific/Kwajalein",
                                      "Pacific/Majuro",
                                      "Pacific/Marquesas",
                                      "Pacific/Midway",
                                      "Pacific/Nauru",
                                      "Pacific/Niue",
                                      "Pacific/Norfolk",
                                      "Pacific/Noumea",
                                      "Pacific/Pago_Pago",
                                      "Pacific/Palau",
                                      "Pacific/Pitcairn",
                                      "Pacific/Pohnpei",
                                      "Pacific/Port_Moresby",
                                      "Pacific/Rarotonga",
                                      "Pacific/Saipan",
                                      "Pacific/Samoa",
                                      "Pacific/Tahiti",
                                      "Pacific/Tarawa",
                                      "Pacific/Tongatapu",
                                      "Pacific/Wake",
                                      "Pacific/Wallis",
                                      "Pacific/Yap",
                                      "UTC",
                                      nullptr};

std::vector<std::string> AllTimeZoneNames() {
  std::vector<std::string> names;
  for (const char* const* namep = kTimeZoneNames; *namep != nullptr; ++namep) {
    names.push_back(std::string("file:") + *namep);
  }
  assert(!names.empty());

  std::mt19937 urbg(42);  // a UniformRandomBitGenerator with fixed seed
  std::shuffle(names.begin(), names.end(), urbg);
  return names;
}

cctz::time_zone TestTimeZone() {
  cctz::time_zone tz;
  cctz::load_time_zone("America/Los_Angeles", &tz);
  return tz;
}

void BM_Zone_LoadUTCTimeZoneFirst(benchmark::State& state) {
  cctz::time_zone tz;
  cctz::load_time_zone("UTC", &tz);  // in case we're first
  cctz::time_zone::Impl::ClearTimeZoneMapTestOnly();
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(cctz::load_time_zone("UTC", &tz));
  }
}
BENCHMARK(BM_Zone_LoadUTCTimeZoneFirst);

void BM_Zone_LoadUTCTimeZoneLast(benchmark::State& state) {
  cctz::time_zone tz;
  for (const auto& name : AllTimeZoneNames()) {
    cctz::load_time_zone(name, &tz);  // prime cache
  }
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(cctz::load_time_zone("UTC", &tz));
  }
}
BENCHMARK(BM_Zone_LoadUTCTimeZoneLast);

void BM_Zone_LoadTimeZoneFirst(benchmark::State& state) {
  cctz::time_zone tz = cctz::utc_time_zone();  // in case we're first
  const std::string name = "file:America/Los_Angeles";
  while (state.KeepRunning()) {
    state.PauseTiming();
    cctz::time_zone::Impl::ClearTimeZoneMapTestOnly();
    state.ResumeTiming();
    benchmark::DoNotOptimize(cctz::load_time_zone(name, &tz));
  }
}
BENCHMARK(BM_Zone_LoadTimeZoneFirst);

void BM_Zone_LoadTimeZoneCached(benchmark::State& state) {
  cctz::time_zone tz = cctz::utc_time_zone();  // in case we're first
  cctz::time_zone::Impl::ClearTimeZoneMapTestOnly();
  const std::string name = "file:America/Los_Angeles";
  cctz::load_time_zone(name, &tz);  // prime cache
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(cctz::load_time_zone(name, &tz));
  }
}
BENCHMARK(BM_Zone_LoadTimeZoneCached);

void BM_Zone_LoadLocalTimeZoneCached(benchmark::State& state) {
  cctz::utc_time_zone();  // in case we're first
  cctz::time_zone::Impl::ClearTimeZoneMapTestOnly();
  cctz::local_time_zone();  // prime cache
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(cctz::local_time_zone());
  }
}
BENCHMARK(BM_Zone_LoadLocalTimeZoneCached);

void BM_Zone_LoadAllTimeZonesFirst(benchmark::State& state) {
  cctz::time_zone tz;
  const std::vector<std::string> names = AllTimeZoneNames();
  for (auto index = names.size(); state.KeepRunning(); ++index) {
    if (index == names.size()) {
      index = 0;
    }
    if (index == 0) {
      state.PauseTiming();
      cctz::time_zone::Impl::ClearTimeZoneMapTestOnly();
      state.ResumeTiming();
    }
    benchmark::DoNotOptimize(cctz::load_time_zone(names[index], &tz));
  }
}
BENCHMARK(BM_Zone_LoadAllTimeZonesFirst);

void BM_Zone_LoadAllTimeZonesCached(benchmark::State& state) {
  cctz::time_zone tz;
  const std::vector<std::string> names = AllTimeZoneNames();
  for (const auto& name : names) {
    cctz::load_time_zone(name, &tz);  // prime cache
  }
  for (auto index = names.size(); state.KeepRunning(); ++index) {
    if (index == names.size()) {
      index = 0;
    }
    benchmark::DoNotOptimize(cctz::load_time_zone(names[index], &tz));
  }
}
BENCHMARK(BM_Zone_LoadAllTimeZonesCached);

void BM_Zone_TimeZoneEqualityImplicit(benchmark::State& state) {
  cctz::time_zone tz;  // implicit UTC
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(tz == tz);
  }
}
BENCHMARK(BM_Zone_TimeZoneEqualityImplicit);

void BM_Zone_TimeZoneEqualityExplicit(benchmark::State& state) {
  cctz::time_zone tz = cctz::utc_time_zone();  // explicit UTC
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(tz == tz);
  }
}
BENCHMARK(BM_Zone_TimeZoneEqualityExplicit);

void BM_Zone_UTCTimeZone(benchmark::State& state) {
  cctz::time_zone tz;
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(cctz::utc_time_zone());
  }
}
BENCHMARK(BM_Zone_UTCTimeZone);

// In each "ToCivil" benchmark we switch between two instants separated
// by at least one transition in order to defeat any internal caching of
// previous results (e.g., see local_time_hint_).
//
// The "UTC" variants use UTC instead of the Google/local time zone.

void BM_Time_ToCivil_CCTZ(benchmark::State& state) {
  const cctz::time_zone tz = TestTimeZone();
  std::chrono::system_clock::time_point tp =
      std::chrono::system_clock::from_time_t(1384569027);
  std::chrono::system_clock::time_point tp2 =
      std::chrono::system_clock::from_time_t(1418962578);
  while (state.KeepRunning()) {
    std::swap(tp, tp2);
    tp += std::chrono::seconds(1);
    benchmark::DoNotOptimize(cctz::convert(tp, tz));
  }
}
BENCHMARK(BM_Time_ToCivil_CCTZ);

void BM_Time_ToCivil_Libc(benchmark::State& state) {
  // No timezone support, so just use localtime.
  time_t t = 1384569027;
  time_t t2 = 1418962578;
  struct tm tm;
  while (state.KeepRunning()) {
    std::swap(t, t2);
    t += 1;
#if defined(_WIN32) || defined(_WIN64)
    benchmark::DoNotOptimize(localtime_s(&tm, &t));
#else
    benchmark::DoNotOptimize(localtime_r(&t, &tm));
#endif
  }
}
BENCHMARK(BM_Time_ToCivil_Libc);

void BM_Time_ToCivilUTC_CCTZ(benchmark::State& state) {
  const cctz::time_zone tz = cctz::utc_time_zone();
  std::chrono::system_clock::time_point tp =
      std::chrono::system_clock::from_time_t(1384569027);
  while (state.KeepRunning()) {
    tp += std::chrono::seconds(1);
    benchmark::DoNotOptimize(cctz::convert(tp, tz));
  }
}
BENCHMARK(BM_Time_ToCivilUTC_CCTZ);

void BM_Time_ToCivilUTC_Libc(benchmark::State& state) {
  time_t t = 1384569027;
  struct tm tm;
  while (state.KeepRunning()) {
    t += 1;
#if defined(_WIN32) || defined(_WIN64)
    benchmark::DoNotOptimize(gmtime_s(&tm, &t));
#else
    benchmark::DoNotOptimize(gmtime_r(&t, &tm));
#endif
  }
}
BENCHMARK(BM_Time_ToCivilUTC_Libc);

// In each "FromCivil" benchmark we switch between two YMDhms values
// separated by at least one transition in order to defeat any internal
// caching of previous results (e.g., see time_local_hint_).
//
// The "UTC" variants use UTC instead of the Google/local time zone.
// The "Day0" variants require normalization of the day of month.

void BM_Time_FromCivil_CCTZ(benchmark::State& state) {
  const cctz::time_zone tz = TestTimeZone();
  int i = 0;
  while (state.KeepRunning()) {
    if ((i++ & 1) == 0) {
      benchmark::DoNotOptimize(
          cctz::convert(cctz::civil_second(2014, 12, 18, 20, 16, 18), tz));
    } else {
      benchmark::DoNotOptimize(
          cctz::convert(cctz::civil_second(2013, 11, 15, 18, 30, 27), tz));
    }
  }
}
BENCHMARK(BM_Time_FromCivil_CCTZ);

void BM_Time_FromCivil_Libc(benchmark::State& state) {
  // No timezone support, so just use localtime.
  int i = 0;
  while (state.KeepRunning()) {
    struct tm tm;
    if ((i++ & 1) == 0) {
      tm.tm_year = 2014 - 1900;
      tm.tm_mon = 12 - 1;
      tm.tm_mday = 18;
      tm.tm_hour = 20;
      tm.tm_min = 16;
      tm.tm_sec = 18;
    } else {
      tm.tm_year = 2013 - 1900;
      tm.tm_mon = 11 - 1;
      tm.tm_mday = 15;
      tm.tm_hour = 18;
      tm.tm_min = 30;
      tm.tm_sec = 27;
    }
    tm.tm_isdst = -1;
    benchmark::DoNotOptimize(mktime(&tm));
  }
}
BENCHMARK(BM_Time_FromCivil_Libc);

void BM_Time_FromCivilUTC_CCTZ(benchmark::State& state) {
  const cctz::time_zone tz = cctz::utc_time_zone();
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(
        cctz::convert(cctz::civil_second(2014, 12, 18, 20, 16, 18), tz));
  }
}
BENCHMARK(BM_Time_FromCivilUTC_CCTZ);

// There is no BM_Time_FromCivilUTC_Libc.

void BM_Time_FromCivilDay0_CCTZ(benchmark::State& state) {
  const cctz::time_zone tz = TestTimeZone();
  int i = 0;
  while (state.KeepRunning()) {
    if ((i++ & 1) == 0) {
      benchmark::DoNotOptimize(
          cctz::convert(cctz::civil_second(2014, 12, 0, 20, 16, 18), tz));
    } else {
      benchmark::DoNotOptimize(
          cctz::convert(cctz::civil_second(2013, 11, 0, 18, 30, 27), tz));
    }
  }
}
BENCHMARK(BM_Time_FromCivilDay0_CCTZ);

void BM_Time_FromCivilDay0_Libc(benchmark::State& state) {
  // No timezone support, so just use localtime.
  int i = 0;
  while (state.KeepRunning()) {
    struct tm tm;
    if ((i++ & 1) == 0) {
      tm.tm_year = 2014 - 1900;
      tm.tm_mon = 12 - 1;
      tm.tm_mday = 0;
      tm.tm_hour = 20;
      tm.tm_min = 16;
      tm.tm_sec = 18;
    } else {
      tm.tm_year = 2013 - 1900;
      tm.tm_mon = 11 - 1;
      tm.tm_mday = 0;
      tm.tm_hour = 18;
      tm.tm_min = 30;
      tm.tm_sec = 27;
    }
    tm.tm_isdst = -1;
    benchmark::DoNotOptimize(mktime(&tm));
  }
}
BENCHMARK(BM_Time_FromCivilDay0_Libc);

const char* const kFormats[] = {
    RFC1123_full,           // 0
    RFC1123_no_wday,        // 1
    RFC3339_full,           // 2
    RFC3339_sec,            // 3
    "%Y-%m-%d%ET%H:%M:%S",  // 4
    "%Y-%m-%d",             // 5
};
const int kNumFormats = sizeof(kFormats) / sizeof(kFormats[0]);

void BM_Format_FormatTime(benchmark::State& state) {
  const std::string fmt = kFormats[state.range(0)];
  state.SetLabel(fmt);
  const cctz::time_zone tz = TestTimeZone();
  const std::chrono::system_clock::time_point tp =
      cctz::convert(cctz::civil_second(1977, 6, 28, 9, 8, 7), tz) +
      std::chrono::microseconds(1);
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(cctz::format(fmt, tp, tz));
  }
}
BENCHMARK(BM_Format_FormatTime)->DenseRange(0, kNumFormats - 1);

void BM_Format_ParseTime(benchmark::State& state) {
  const std::string fmt = kFormats[state.range(0)];
  state.SetLabel(fmt);
  const cctz::time_zone tz = TestTimeZone();
  std::chrono::system_clock::time_point tp =
      cctz::convert(cctz::civil_second(1977, 6, 28, 9, 8, 7), tz) +
      std::chrono::microseconds(1);
  const std::string when = cctz::format(fmt, tp, tz);
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(cctz::parse(fmt, when, tz, &tp));
  }
}
BENCHMARK(BM_Format_ParseTime)->DenseRange(0, kNumFormats - 1);

}  // namespace
