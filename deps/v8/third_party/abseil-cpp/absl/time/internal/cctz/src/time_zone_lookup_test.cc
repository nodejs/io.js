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

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <future>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "absl/base/config.h"
#include "absl/time/internal/cctz/include/cctz/time_zone.h"
#if defined(__linux__)
#include <features.h>
#endif

#include "gtest/gtest.h"
#include "absl/time/internal/cctz/include/cctz/civil_time.h"

namespace chrono = std::chrono;

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {

namespace {

// A list of known time-zone names.
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

// Helper to return a loaded time zone by value (UTC on error).
time_zone LoadZone(const std::string& name) {
  time_zone tz;
  load_time_zone(name, &tz);
  return tz;
}

// This helper is a macro so that failed expectations show up with the
// correct line numbers.
#define ExpectTime(tp, tz, y, m, d, hh, mm, ss, off, isdst, zone) \
  do {                                                            \
    time_zone::absolute_lookup al = tz.lookup(tp);                \
    EXPECT_EQ(y, al.cs.year());                                   \
    EXPECT_EQ(m, al.cs.month());                                  \
    EXPECT_EQ(d, al.cs.day());                                    \
    EXPECT_EQ(hh, al.cs.hour());                                  \
    EXPECT_EQ(mm, al.cs.minute());                                \
    EXPECT_EQ(ss, al.cs.second());                                \
    EXPECT_EQ(off, al.offset);                                    \
    EXPECT_TRUE(isdst == al.is_dst);                              \
    /* EXPECT_STREQ(zone, al.abbr); */                            \
  } while (0)

// These tests sometimes run on platforms that have zoneinfo data so old
// that the transition we are attempting to check does not exist, most
// notably Android emulators.  Fortunately, AndroidZoneInfoSource supports
// time_zone::version() so, in cases where we've learned that it matters,
// we can make the check conditionally.
int VersionCmp(time_zone tz, const std::string& target) {
  std::string version = tz.version();
  if (version.empty() && !target.empty()) return 1;  // unknown > known
  return version.compare(target);
}

}  // namespace

#if !defined(__EMSCRIPTEN__)
TEST(TimeZones, LoadZonesConcurrently) {
  std::promise<void> ready_promise;
  std::shared_future<void> ready_future(ready_promise.get_future());
  auto load_zones = [ready_future](std::promise<void>* started,
                                   std::set<std::string>* failures) {
    started->set_value();
    ready_future.wait();
    for (const char* const* np = kTimeZoneNames; *np != nullptr; ++np) {
      std::string zone = *np;
      time_zone tz;
      if (load_time_zone(zone, &tz)) {
        EXPECT_EQ(zone, tz.name());
      } else {
        failures->insert(zone);
      }
    }
  };

  const std::size_t n_threads = 128;
  std::vector<std::thread> threads;
  std::vector<std::set<std::string>> thread_failures(n_threads);
  for (std::size_t i = 0; i != n_threads; ++i) {
    std::promise<void> started;
    threads.emplace_back(load_zones, &started, &thread_failures[i]);
    started.get_future().wait();
  }
  ready_promise.set_value();
  for (auto& thread : threads) {
    thread.join();
  }

  // Allow a small number of failures to account for skew between
  // the contents of kTimeZoneNames and the zoneinfo data source.
#if defined(__ANDROID__)
  // Cater to the possibility of using an even older zoneinfo data
  // source when running on Android, where it is difficult to override
  // the bionic tzdata provided by the test environment.
  const std::size_t max_failures = 20;
#else
  const std::size_t max_failures = 3;
#endif
  std::set<std::string> failures;
  for (const auto& thread_failure : thread_failures) {
    failures.insert(thread_failure.begin(), thread_failure.end());
  }
  EXPECT_LE(failures.size(), max_failures) << testing::PrintToString(failures);
}
#endif

TEST(TimeZone, UTC) {
  const time_zone utc = utc_time_zone();

  time_zone loaded_utc;
  EXPECT_TRUE(load_time_zone("UTC", &loaded_utc));
  EXPECT_EQ(loaded_utc, utc);

  time_zone loaded_utc0;
  EXPECT_TRUE(load_time_zone("UTC0", &loaded_utc0));
  EXPECT_EQ(loaded_utc0, utc);

  time_zone loaded_bad;
  EXPECT_FALSE(load_time_zone("Invalid/TimeZone", &loaded_bad));
  EXPECT_EQ(loaded_bad, utc);
}

TEST(TimeZone, NamedTimeZones) {
  const time_zone utc = utc_time_zone();
  EXPECT_EQ("UTC", utc.name());
  const time_zone nyc = LoadZone("America/New_York");
  EXPECT_EQ("America/New_York", nyc.name());
  const time_zone syd = LoadZone("Australia/Sydney");
  EXPECT_EQ("Australia/Sydney", syd.name());
  const time_zone fixed0 =
      fixed_time_zone(absl::time_internal::cctz::seconds::zero());
  EXPECT_EQ("UTC", fixed0.name());
  const time_zone fixed_pos = fixed_time_zone(
      chrono::hours(3) + chrono::minutes(25) + chrono::seconds(45));
  EXPECT_EQ("Fixed/UTC+03:25:45", fixed_pos.name());
  const time_zone fixed_neg = fixed_time_zone(
      -(chrono::hours(12) + chrono::minutes(34) + chrono::seconds(56)));
  EXPECT_EQ("Fixed/UTC-12:34:56", fixed_neg.name());
}

TEST(TimeZone, Failures) {
  time_zone tz;
  EXPECT_FALSE(load_time_zone(":America/Los_Angeles", &tz));

  tz = LoadZone("America/Los_Angeles");
  EXPECT_FALSE(load_time_zone("Invalid/TimeZone", &tz));
  EXPECT_EQ(chrono::system_clock::from_time_t(0),
            convert(civil_second(1970, 1, 1, 0, 0, 0), tz));  // UTC

  // Ensures that the load still fails on a subsequent attempt.
  tz = LoadZone("America/Los_Angeles");
  EXPECT_FALSE(load_time_zone("Invalid/TimeZone", &tz));
  EXPECT_EQ(chrono::system_clock::from_time_t(0),
            convert(civil_second(1970, 1, 1, 0, 0, 0), tz));  // UTC

  // Loading an empty string timezone should fail.
  tz = LoadZone("America/Los_Angeles");
  EXPECT_FALSE(load_time_zone("", &tz));
  EXPECT_EQ(chrono::system_clock::from_time_t(0),
            convert(civil_second(1970, 1, 1, 0, 0, 0), tz));  // UTC
}

TEST(TimeZone, Equality) {
  const time_zone a;
  const time_zone b;
  EXPECT_EQ(a, b);
  EXPECT_EQ(a.name(), b.name());

  const time_zone implicit_utc;
  const time_zone explicit_utc = utc_time_zone();
  EXPECT_EQ(implicit_utc, explicit_utc);
  EXPECT_EQ(implicit_utc.name(), explicit_utc.name());

  const time_zone fixed_zero =
      fixed_time_zone(absl::time_internal::cctz::seconds::zero());
  EXPECT_EQ(fixed_zero, LoadZone(fixed_zero.name()));
  EXPECT_EQ(fixed_zero, explicit_utc);

  const time_zone fixed_utc = LoadZone("Fixed/UTC+00:00:00");
  EXPECT_EQ(fixed_utc, LoadZone(fixed_utc.name()));
  EXPECT_EQ(fixed_utc, explicit_utc);

  const time_zone fixed_pos = fixed_time_zone(
      chrono::hours(3) + chrono::minutes(25) + chrono::seconds(45));
  EXPECT_EQ(fixed_pos, LoadZone(fixed_pos.name()));
  EXPECT_NE(fixed_pos, explicit_utc);
  const time_zone fixed_neg = fixed_time_zone(
      -(chrono::hours(12) + chrono::minutes(34) + chrono::seconds(56)));
  EXPECT_EQ(fixed_neg, LoadZone(fixed_neg.name()));
  EXPECT_NE(fixed_neg, explicit_utc);

  const time_zone fixed_lim = fixed_time_zone(chrono::hours(24));
  EXPECT_EQ(fixed_lim, LoadZone(fixed_lim.name()));
  EXPECT_NE(fixed_lim, explicit_utc);
  const time_zone fixed_ovfl =
      fixed_time_zone(chrono::hours(24) + chrono::seconds(1));
  EXPECT_EQ(fixed_ovfl, LoadZone(fixed_ovfl.name()));
  EXPECT_EQ(fixed_ovfl, explicit_utc);

  EXPECT_EQ(fixed_time_zone(chrono::seconds(1)),
            fixed_time_zone(chrono::seconds(1)));

  const time_zone local = local_time_zone();
  EXPECT_EQ(local, LoadZone(local.name()));

  time_zone la = LoadZone("America/Los_Angeles");
  time_zone nyc = LoadZone("America/New_York");
  EXPECT_NE(la, nyc);
}

TEST(StdChronoTimePoint, TimeTAlignment) {
  // Ensures that the Unix epoch and the system clock epoch are an integral
  // number of seconds apart. This simplifies conversions to/from time_t.
  auto diff =
      chrono::system_clock::time_point() - chrono::system_clock::from_time_t(0);
  EXPECT_EQ(chrono::system_clock::time_point::duration::zero(),
            diff % chrono::seconds(1));
}

TEST(BreakTime, TimePointResolution) {
  const time_zone utc = utc_time_zone();
  const auto t0 = chrono::system_clock::from_time_t(0);

  ExpectTime(chrono::time_point_cast<chrono::nanoseconds>(t0), utc, 1970, 1, 1,
             0, 0, 0, 0, false, "UTC");
  ExpectTime(chrono::time_point_cast<chrono::microseconds>(t0), utc, 1970, 1, 1,
             0, 0, 0, 0, false, "UTC");
  ExpectTime(chrono::time_point_cast<chrono::milliseconds>(t0), utc, 1970, 1, 1,
             0, 0, 0, 0, false, "UTC");
  ExpectTime(chrono::time_point_cast<chrono::seconds>(t0), utc, 1970, 1, 1, 0,
             0, 0, 0, false, "UTC");
  ExpectTime(chrono::time_point_cast<absl::time_internal::cctz::seconds>(t0),
             utc, 1970, 1, 1, 0, 0, 0, 0, false, "UTC");
  ExpectTime(chrono::time_point_cast<chrono::minutes>(t0), utc, 1970, 1, 1, 0,
             0, 0, 0, false, "UTC");
  ExpectTime(chrono::time_point_cast<chrono::hours>(t0), utc, 1970, 1, 1, 0, 0,
             0, 0, false, "UTC");
}

TEST(BreakTime, LocalTimeInUTC) {
  const time_zone tz = utc_time_zone();
  const auto tp = chrono::system_clock::from_time_t(0);
  ExpectTime(tp, tz, 1970, 1, 1, 0, 0, 0, 0, false, "UTC");
  EXPECT_EQ(weekday::thursday, get_weekday(convert(tp, tz)));
}

TEST(BreakTime, LocalTimeInUTCUnaligned) {
  const time_zone tz = utc_time_zone();
  const auto tp =
      chrono::system_clock::from_time_t(0) - chrono::milliseconds(500);
  ExpectTime(tp, tz, 1969, 12, 31, 23, 59, 59, 0, false, "UTC");
  EXPECT_EQ(weekday::wednesday, get_weekday(convert(tp, tz)));
}

TEST(BreakTime, LocalTimePosix) {
  // See IEEE Std 1003.1-1988 B.2.3 General Terms, Epoch.
  const time_zone tz = utc_time_zone();
  const auto tp = chrono::system_clock::from_time_t(536457599);
  ExpectTime(tp, tz, 1986, 12, 31, 23, 59, 59, 0, false, "UTC");
  EXPECT_EQ(weekday::wednesday, get_weekday(convert(tp, tz)));
}

TEST(TimeZoneImpl, LocalTimeInFixed) {
  const absl::time_internal::cctz::seconds offset =
      -(chrono::hours(8) + chrono::minutes(33) + chrono::seconds(47));
  const time_zone tz = fixed_time_zone(offset);
  const auto tp = chrono::system_clock::from_time_t(0);
  ExpectTime(tp, tz, 1969, 12, 31, 15, 26, 13, offset.count(), false,
             "-083347");
  EXPECT_EQ(weekday::wednesday, get_weekday(convert(tp, tz)));
}

TEST(BreakTime, LocalTimeInNewYork) {
  const time_zone tz = LoadZone("America/New_York");
  const auto tp = chrono::system_clock::from_time_t(45);
  ExpectTime(tp, tz, 1969, 12, 31, 19, 0, 45, -5 * 60 * 60, false, "EST");
  EXPECT_EQ(weekday::wednesday, get_weekday(convert(tp, tz)));
}

TEST(BreakTime, LocalTimeInMTV) {
  const time_zone tz = LoadZone("America/Los_Angeles");
  const auto tp = chrono::system_clock::from_time_t(1380855729);
  ExpectTime(tp, tz, 2013, 10, 3, 20, 2, 9, -7 * 60 * 60, true, "PDT");
  EXPECT_EQ(weekday::thursday, get_weekday(convert(tp, tz)));
}

TEST(BreakTime, LocalTimeInSydney) {
  const time_zone tz = LoadZone("Australia/Sydney");
  const auto tp = chrono::system_clock::from_time_t(90);
  ExpectTime(tp, tz, 1970, 1, 1, 10, 1, 30, 10 * 60 * 60, false, "AEST");
  EXPECT_EQ(weekday::thursday, get_weekday(convert(tp, tz)));
}

TEST(MakeTime, TimePointResolution) {
  const time_zone utc = utc_time_zone();
  const time_point<chrono::nanoseconds> tp_ns =
      convert(civil_second(2015, 1, 2, 3, 4, 5), utc);
  EXPECT_EQ("04:05", absl::time_internal::cctz::format("%M:%E*S", tp_ns, utc));
  const time_point<chrono::microseconds> tp_us =
      convert(civil_second(2015, 1, 2, 3, 4, 5), utc);
  EXPECT_EQ("04:05", absl::time_internal::cctz::format("%M:%E*S", tp_us, utc));
  const time_point<chrono::milliseconds> tp_ms =
      convert(civil_second(2015, 1, 2, 3, 4, 5), utc);
  EXPECT_EQ("04:05", absl::time_internal::cctz::format("%M:%E*S", tp_ms, utc));
  const time_point<chrono::seconds> tp_s =
      convert(civil_second(2015, 1, 2, 3, 4, 5), utc);
  EXPECT_EQ("04:05", absl::time_internal::cctz::format("%M:%E*S", tp_s, utc));
  const time_point<absl::time_internal::cctz::seconds> tp_s64 =
      convert(civil_second(2015, 1, 2, 3, 4, 5), utc);
  EXPECT_EQ("04:05", absl::time_internal::cctz::format("%M:%E*S", tp_s64, utc));

  // These next two require chrono::time_point_cast because the conversion
  // from a resolution of seconds (the return value of convert()) to a
  // coarser resolution requires an explicit cast.
  const time_point<chrono::minutes> tp_m =
      chrono::time_point_cast<chrono::minutes>(
          convert(civil_second(2015, 1, 2, 3, 4, 5), utc));
  EXPECT_EQ("04:00", absl::time_internal::cctz::format("%M:%E*S", tp_m, utc));
  const time_point<chrono::hours> tp_h = chrono::time_point_cast<chrono::hours>(
      convert(civil_second(2015, 1, 2, 3, 4, 5), utc));
  EXPECT_EQ("00:00", absl::time_internal::cctz::format("%M:%E*S", tp_h, utc));
}

TEST(MakeTime, Normalization) {
  const time_zone tz = LoadZone("America/New_York");
  const auto tp = convert(civil_second(2009, 2, 13, 18, 31, 30), tz);
  EXPECT_EQ(chrono::system_clock::from_time_t(1234567890), tp);

  // Now requests for the same time_point but with out-of-range fields.
  EXPECT_EQ(tp, convert(civil_second(2008, 14, 13, 18, 31, 30), tz));  // month
  EXPECT_EQ(tp, convert(civil_second(2009, 1, 44, 18, 31, 30), tz));   // day
  EXPECT_EQ(tp, convert(civil_second(2009, 2, 12, 42, 31, 30), tz));   // hour
  EXPECT_EQ(tp, convert(civil_second(2009, 2, 13, 17, 91, 30), tz));   // minute
  EXPECT_EQ(tp, convert(civil_second(2009, 2, 13, 18, 30, 90), tz));   // second
}

// NOTE: Run this with -ftrapv to detect overflow problems.
TEST(MakeTime, SysSecondsLimits) {
  const char RFC3339[] = "%Y-%m-%d%ET%H:%M:%S%Ez";
  const time_zone utc = utc_time_zone();
  const time_zone east = fixed_time_zone(chrono::hours(14));
  const time_zone west = fixed_time_zone(-chrono::hours(14));
  time_point<absl::time_internal::cctz::seconds> tp;

  // Approach the maximal time_point<cctz::seconds> value from below.
  tp = convert(civil_second(292277026596, 12, 4, 15, 30, 6), utc);
  EXPECT_EQ("292277026596-12-04T15:30:06+00:00",
            absl::time_internal::cctz::format(RFC3339, tp, utc));
  tp = convert(civil_second(292277026596, 12, 4, 15, 30, 7), utc);
  EXPECT_EQ("292277026596-12-04T15:30:07+00:00",
            absl::time_internal::cctz::format(RFC3339, tp, utc));
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::max(), tp);
  tp = convert(civil_second(292277026596, 12, 4, 15, 30, 8), utc);
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::max(), tp);
  tp = convert(civil_second::max(), utc);
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::max(), tp);

  // Checks that we can also get the maximal value for a far-east zone.
  tp = convert(civil_second(292277026596, 12, 5, 5, 30, 7), east);
  EXPECT_EQ("292277026596-12-05T05:30:07+14:00",
            absl::time_internal::cctz::format(RFC3339, tp, east));
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::max(), tp);
  tp = convert(civil_second(292277026596, 12, 5, 5, 30, 8), east);
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::max(), tp);
  tp = convert(civil_second::max(), east);
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::max(), tp);

  // Checks that we can also get the maximal value for a far-west zone.
  tp = convert(civil_second(292277026596, 12, 4, 1, 30, 7), west);
  EXPECT_EQ("292277026596-12-04T01:30:07-14:00",
            absl::time_internal::cctz::format(RFC3339, tp, west));
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::max(), tp);
  tp = convert(civil_second(292277026596, 12, 4, 7, 30, 8), west);
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::max(), tp);
  tp = convert(civil_second::max(), west);
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::max(), tp);

  // Approach the minimal time_point<cctz::seconds> value from above.
  tp = convert(civil_second(-292277022657, 1, 27, 8, 29, 53), utc);
  EXPECT_EQ("-292277022657-01-27T08:29:53+00:00",
            absl::time_internal::cctz::format(RFC3339, tp, utc));
  tp = convert(civil_second(-292277022657, 1, 27, 8, 29, 52), utc);
  EXPECT_EQ("-292277022657-01-27T08:29:52+00:00",
            absl::time_internal::cctz::format(RFC3339, tp, utc));
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::min(), tp);
  tp = convert(civil_second(-292277022657, 1, 27, 8, 29, 51), utc);
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::min(), tp);
  tp = convert(civil_second::min(), utc);
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::min(), tp);

  // Checks that we can also get the minimal value for a far-east zone.
  tp = convert(civil_second(-292277022657, 1, 27, 22, 29, 52), east);
  EXPECT_EQ("-292277022657-01-27T22:29:52+14:00",
            absl::time_internal::cctz::format(RFC3339, tp, east));
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::min(), tp);
  tp = convert(civil_second(-292277022657, 1, 27, 22, 29, 51), east);
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::min(), tp);
  tp = convert(civil_second::min(), east);
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::min(), tp);

  // Checks that we can also get the minimal value for a far-west zone.
  tp = convert(civil_second(-292277022657, 1, 26, 18, 29, 52), west);
  EXPECT_EQ("-292277022657-01-26T18:29:52-14:00",
            absl::time_internal::cctz::format(RFC3339, tp, west));
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::min(), tp);
  tp = convert(civil_second(-292277022657, 1, 26, 18, 29, 51), west);
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::min(), tp);
  tp = convert(civil_second::min(), west);
  EXPECT_EQ(time_point<absl::time_internal::cctz::seconds>::min(), tp);

  // Some similar checks for the "libc" time-zone implementation.
  if (sizeof(std::time_t) >= 8) {
    // Checks that "tm_year + 1900", as used by the "libc" implementation,
    // can produce year values beyond the range on an int without overflow.
#if defined(_WIN32) || defined(_WIN64)
    // localtime_s() and gmtime_s() don't believe in years outside [1970:3000].
#else
    const time_zone cut = LoadZone("libc:UTC");
    const year_t max_tm_year = year_t{std::numeric_limits<int>::max()} + 1900;
    tp = convert(civil_second(max_tm_year, 12, 31, 23, 59, 59), cut);
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__EMSCRIPTEN__)
    // Some gmtime_r() impls fail on extreme positive values.
#else
    EXPECT_EQ("2147485547-12-31T23:59:59+00:00",
              absl::time_internal::cctz::format(RFC3339, tp, cut));
#endif
    const year_t min_tm_year = year_t{std::numeric_limits<int>::min()} + 1900;
    tp = convert(civil_second(min_tm_year, 1, 1, 0, 0, 0), cut);
#if defined(__Fuchsia__) || defined(__EMSCRIPTEN__)
    // Some gmtime_r() impls fail on extreme negative values (fxbug.dev/78527).
#else
    EXPECT_EQ("-2147481748-01-01T00:00:00+00:00",
              absl::time_internal::cctz::format(RFC3339, tp, cut));
#endif
#endif
  }
}

TEST(MakeTime, LocalTimeLibC) {
  // Checks that cctz and libc agree on transition points in [1970:2037].
  //
  // We limit this test case to environments where:
  //  1) we know how to change the time zone used by localtime()/mktime(),
  //  2) cctz and localtime()/mktime() will use similar-enough tzdata, and
  //  3) we have some idea about how mktime() behaves during transitions.
#if defined(__linux__) && defined(__GLIBC__) && !defined(__ANDROID__)
  const char* const ep = getenv("TZ");
  std::string tz_name = (ep != nullptr) ? ep : "";
  for (const char* const* np = kTimeZoneNames; *np != nullptr; ++np) {
    ASSERT_EQ(0, setenv("TZ", *np, 1));  // change what "localtime" means
    const auto zi = local_time_zone();
    const auto lc = LoadZone("libc:localtime");
    time_zone::civil_transition transition;
    for (auto tp = zi.lookup(civil_second()).trans;
         zi.next_transition(tp, &transition);
         tp = zi.lookup(transition.to).trans) {
      const auto fcl = zi.lookup(transition.from);
      const auto tcl = zi.lookup(transition.to);
      civil_second cs, us;  // compare cs and us in zi and lc
      if (fcl.kind == time_zone::civil_lookup::UNIQUE) {
        if (tcl.kind == time_zone::civil_lookup::UNIQUE) {
          // Both unique; must be an is_dst or abbr change.
          ASSERT_EQ(transition.from, transition.to);
          const auto trans = fcl.trans;
          const auto tal = zi.lookup(trans);
          const auto tprev = trans - absl::time_internal::cctz::seconds(1);
          const auto pal = zi.lookup(tprev);
          if (pal.is_dst == tal.is_dst) {
            ASSERT_STRNE(pal.abbr, tal.abbr);
          }
          continue;
        }
        ASSERT_EQ(time_zone::civil_lookup::REPEATED, tcl.kind);
        cs = transition.to;
        us = transition.from;
      } else {
        ASSERT_EQ(time_zone::civil_lookup::UNIQUE, tcl.kind);
        ASSERT_EQ(time_zone::civil_lookup::SKIPPED, fcl.kind);
        cs = transition.from;
        us = transition.to;
      }
      if (us.year() > 2037) break;  // limit test time (and to 32-bit time_t)
      const auto cl_zi = zi.lookup(cs);
      if (zi.lookup(cl_zi.pre).is_dst == zi.lookup(cl_zi.post).is_dst) {
        // The "libc" implementation cannot correctly classify transitions
        // that don't change the "tm_isdst" flag.  In Europe/Volgograd, for
        // example, there is a SKIPPED transition from +03 to +04 with dst=F
        // on both sides ...
        //   1540681199 = 2018-10-28 01:59:59 +03:00:00 [dst=F off=10800]
        //   1540681200 = 2018-10-28 03:00:00 +04:00:00 [dst=F off=14400]
        // but std::mktime(2018-10-28 02:00:00, tm_isdst=0) fails, unlike,
        // say, the similar Europe/Chisinau transition from +02 to +03 ...
        //   1521935999 = 2018-03-25 01:59:59 +02:00:00 [dst=F off=7200]
        //   1521936000 = 2018-03-25 03:00:00 +03:00:00 [dst=T off=10800]
        // where std::mktime(2018-03-25 02:00:00, tm_isdst=0) succeeds and
        // returns 1521936000.
        continue;
      }
      if (cs == civil_second(2037, 10, 4, 2, 0, 0)) {
        const std::string tzname = *np;
        if (tzname == "Africa/Casablanca" || tzname == "Africa/El_Aaiun") {
          // The "libc" implementation gets this transition wrong (at least
          // until 2018g when it was removed), returning an offset of 3600
          // instead of 0.  TODO: Revert this when 2018g is ubiquitous.
          continue;
        }
      }
      const auto cl_lc = lc.lookup(cs);
      SCOPED_TRACE(testing::Message() << "For " << cs << " in " << *np);
      EXPECT_EQ(cl_zi.kind, cl_lc.kind);
      EXPECT_EQ(cl_zi.pre, cl_lc.pre);
      EXPECT_EQ(cl_zi.trans, cl_lc.trans);
      EXPECT_EQ(cl_zi.post, cl_lc.post);
      const auto ucl_zi = zi.lookup(us);
      const auto ucl_lc = lc.lookup(us);
      SCOPED_TRACE(testing::Message() << "For " << us << " in " << *np);
      EXPECT_EQ(ucl_zi.kind, ucl_lc.kind);
      EXPECT_EQ(ucl_zi.pre, ucl_lc.pre);
      EXPECT_EQ(ucl_zi.trans, ucl_lc.trans);
      EXPECT_EQ(ucl_zi.post, ucl_lc.post);
    }
  }
  if (ep == nullptr) {
    ASSERT_EQ(0, unsetenv("TZ"));
  } else {
    ASSERT_EQ(0, setenv("TZ", tz_name.c_str(), 1));
  }
#endif
}

TEST(NextTransition, UTC) {
  const auto tz = utc_time_zone();
  time_zone::civil_transition trans;

  auto tp = time_point<absl::time_internal::cctz::seconds>::min();
  EXPECT_FALSE(tz.next_transition(tp, &trans));

  tp = time_point<absl::time_internal::cctz::seconds>::max();
  EXPECT_FALSE(tz.next_transition(tp, &trans));
}

TEST(PrevTransition, UTC) {
  const auto tz = utc_time_zone();
  time_zone::civil_transition trans;

  auto tp = time_point<absl::time_internal::cctz::seconds>::max();
  EXPECT_FALSE(tz.prev_transition(tp, &trans));

  tp = time_point<absl::time_internal::cctz::seconds>::min();
  EXPECT_FALSE(tz.prev_transition(tp, &trans));
}

TEST(NextTransition, AmericaNewYork) {
  const auto tz = LoadZone("America/New_York");
  time_zone::civil_transition trans;

  auto tp = convert(civil_second(2018, 6, 30, 0, 0, 0), tz);
  EXPECT_TRUE(tz.next_transition(tp, &trans));
  EXPECT_EQ(civil_second(2018, 11, 4, 2, 0, 0), trans.from);
  EXPECT_EQ(civil_second(2018, 11, 4, 1, 0, 0), trans.to);

  tp = time_point<absl::time_internal::cctz::seconds>::max();
  EXPECT_FALSE(tz.next_transition(tp, &trans));

  tp = time_point<absl::time_internal::cctz::seconds>::min();
  EXPECT_TRUE(tz.next_transition(tp, &trans));
  if (trans.from == civil_second(1918, 3, 31, 2, 0, 0)) {
    // It looks like the tzdata is only 32 bit (probably macOS),
    // which bottoms out at 1901-12-13T20:45:52+00:00.
    EXPECT_EQ(civil_second(1918, 3, 31, 3, 0, 0), trans.to);
  } else {
    EXPECT_EQ(civil_second(1883, 11, 18, 12, 3, 58), trans.from);
    EXPECT_EQ(civil_second(1883, 11, 18, 12, 0, 0), trans.to);
  }
}

TEST(PrevTransition, AmericaNewYork) {
  const auto tz = LoadZone("America/New_York");
  time_zone::civil_transition trans;

  auto tp = convert(civil_second(2018, 6, 30, 0, 0, 0), tz);
  EXPECT_TRUE(tz.prev_transition(tp, &trans));
  EXPECT_EQ(civil_second(2018, 3, 11, 2, 0, 0), trans.from);
  EXPECT_EQ(civil_second(2018, 3, 11, 3, 0, 0), trans.to);

  tp = time_point<absl::time_internal::cctz::seconds>::min();
  EXPECT_FALSE(tz.prev_transition(tp, &trans));

  tp = time_point<absl::time_internal::cctz::seconds>::max();
  EXPECT_TRUE(tz.prev_transition(tp, &trans));
  // We have a transition but we don't know which one.
}

TEST(NextTransition, Scan) {
  for (const char* const* np = kTimeZoneNames; *np != nullptr; ++np) {
    SCOPED_TRACE(testing::Message() << "In " << *np);
    time_zone tz;
    // EXPECT_TRUE(load_time_zone(*np, &tz));
    if (!load_time_zone(*np, &tz)) {
      continue;  // tolerate kTimeZoneNames/zoneinfo skew
    }

    auto tp = time_point<absl::time_internal::cctz::seconds>::min();
    time_zone::civil_transition trans;
    while (tz.next_transition(tp, &trans)) {
      time_zone::civil_lookup from_cl = tz.lookup(trans.from);
      EXPECT_NE(from_cl.kind, time_zone::civil_lookup::REPEATED);
      time_zone::civil_lookup to_cl = tz.lookup(trans.to);
      EXPECT_NE(to_cl.kind, time_zone::civil_lookup::SKIPPED);

      auto trans_tp = to_cl.trans;
      time_zone::absolute_lookup trans_al = tz.lookup(trans_tp);
      EXPECT_EQ(trans_al.cs, trans.to);
      auto pre_trans_tp = trans_tp - absl::time_internal::cctz::seconds(1);
      time_zone::absolute_lookup pre_trans_al = tz.lookup(pre_trans_tp);
      EXPECT_EQ(pre_trans_al.cs + 1, trans.from);

      auto offset_delta = trans_al.offset - pre_trans_al.offset;
      EXPECT_EQ(offset_delta, trans.to - trans.from);
      if (offset_delta == 0) {
        // This "transition" is only an is_dst or abbr change.
        EXPECT_EQ(to_cl.kind, time_zone::civil_lookup::UNIQUE);
        if (trans_al.is_dst == pre_trans_al.is_dst) {
          EXPECT_STRNE(trans_al.abbr, pre_trans_al.abbr);
        }
      }

      tp = trans_tp;  // continue scan from transition
    }
  }
}

TEST(TimeZoneEdgeCase, AmericaNewYork) {
  const time_zone tz = LoadZone("America/New_York");

  // Spring 1:59:59 -> 3:00:00
  auto tp = convert(civil_second(2013, 3, 10, 1, 59, 59), tz);
  ExpectTime(tp, tz, 2013, 3, 10, 1, 59, 59, -5 * 3600, false, "EST");
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 2013, 3, 10, 3, 0, 0, -4 * 3600, true, "EDT");

  // Fall 1:59:59 -> 1:00:00
  tp = convert(civil_second(2013, 11, 3, 1, 59, 59), tz);
  ExpectTime(tp, tz, 2013, 11, 3, 1, 59, 59, -4 * 3600, true, "EDT");
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 2013, 11, 3, 1, 0, 0, -5 * 3600, false, "EST");
}

TEST(TimeZoneEdgeCase, AmericaLosAngeles) {
  const time_zone tz = LoadZone("America/Los_Angeles");

  // Spring 1:59:59 -> 3:00:00
  auto tp = convert(civil_second(2013, 3, 10, 1, 59, 59), tz);
  ExpectTime(tp, tz, 2013, 3, 10, 1, 59, 59, -8 * 3600, false, "PST");
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 2013, 3, 10, 3, 0, 0, -7 * 3600, true, "PDT");

  // Fall 1:59:59 -> 1:00:00
  tp = convert(civil_second(2013, 11, 3, 1, 59, 59), tz);
  ExpectTime(tp, tz, 2013, 11, 3, 1, 59, 59, -7 * 3600, true, "PDT");
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 2013, 11, 3, 1, 0, 0, -8 * 3600, false, "PST");
}

TEST(TimeZoneEdgeCase, ArizonaNoTransition) {
  const time_zone tz = LoadZone("America/Phoenix");

  // No transition in Spring.
  auto tp = convert(civil_second(2013, 3, 10, 1, 59, 59), tz);
  ExpectTime(tp, tz, 2013, 3, 10, 1, 59, 59, -7 * 3600, false, "MST");
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 2013, 3, 10, 2, 0, 0, -7 * 3600, false, "MST");

  // No transition in Fall.
  tp = convert(civil_second(2013, 11, 3, 1, 59, 59), tz);
  ExpectTime(tp, tz, 2013, 11, 3, 1, 59, 59, -7 * 3600, false, "MST");
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 2013, 11, 3, 2, 0, 0, -7 * 3600, false, "MST");
}

TEST(TimeZoneEdgeCase, AsiaKathmandu) {
  const time_zone tz = LoadZone("Asia/Kathmandu");

  // A non-DST offset change from +0530 to +0545
  //
  //   504901799 == Tue, 31 Dec 1985 23:59:59 +0530 (+0530)
  //   504901800 == Wed,  1 Jan 1986 00:15:00 +0545 (+0545)
  auto tp = convert(civil_second(1985, 12, 31, 23, 59, 59), tz);
  ExpectTime(tp, tz, 1985, 12, 31, 23, 59, 59, 5.5 * 3600, false, "+0530");
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 1986, 1, 1, 0, 15, 0, 5.75 * 3600, false, "+0545");
}

TEST(TimeZoneEdgeCase, PacificChatham) {
  const time_zone tz = LoadZone("Pacific/Chatham");

  // One-hour DST offset changes, but at atypical values
  //
  //   1365256799 == Sun,  7 Apr 2013 03:44:59 +1345 (+1345)
  //   1365256800 == Sun,  7 Apr 2013 02:45:00 +1245 (+1245)
  auto tp = convert(civil_second(2013, 4, 7, 3, 44, 59), tz);
  ExpectTime(tp, tz, 2013, 4, 7, 3, 44, 59, 13.75 * 3600, true, "+1345");
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 2013, 4, 7, 2, 45, 0, 12.75 * 3600, false, "+1245");

  //   1380376799 == Sun, 29 Sep 2013 02:44:59 +1245 (+1245)
  //   1380376800 == Sun, 29 Sep 2013 03:45:00 +1345 (+1345)
  tp = convert(civil_second(2013, 9, 29, 2, 44, 59), tz);
  ExpectTime(tp, tz, 2013, 9, 29, 2, 44, 59, 12.75 * 3600, false, "+1245");
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 2013, 9, 29, 3, 45, 0, 13.75 * 3600, true, "+1345");
}

TEST(TimeZoneEdgeCase, AustraliaLordHowe) {
  const time_zone tz = LoadZone("Australia/Lord_Howe");

  // Half-hour DST offset changes
  //
  //   1365260399 == Sun,  7 Apr 2013 01:59:59 +1100 (+11)
  //   1365260400 == Sun,  7 Apr 2013 01:30:00 +1030 (+1030)
  auto tp = convert(civil_second(2013, 4, 7, 1, 59, 59), tz);
  ExpectTime(tp, tz, 2013, 4, 7, 1, 59, 59, 11 * 3600, true, "+11");
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 2013, 4, 7, 1, 30, 0, 10.5 * 3600, false, "+1030");

  //   1380986999 == Sun,  6 Oct 2013 01:59:59 +1030 (+1030)
  //   1380987000 == Sun,  6 Oct 2013 02:30:00 +1100 (+11)
  tp = convert(civil_second(2013, 10, 6, 1, 59, 59), tz);
  ExpectTime(tp, tz, 2013, 10, 6, 1, 59, 59, 10.5 * 3600, false, "+1030");
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 2013, 10, 6, 2, 30, 0, 11 * 3600, true, "+11");
}

TEST(TimeZoneEdgeCase, PacificApia) {
  const time_zone tz = LoadZone("Pacific/Apia");

  // At the end of December 2011, Samoa jumped forward by one day,
  // skipping 30 December from the local calendar, when the nation
  // moved to the west of the International Date Line.
  //
  // A one-day, non-DST offset change
  //
  //   1325239199 == Thu, 29 Dec 2011 23:59:59 -1000 (-10)
  //   1325239200 == Sat, 31 Dec 2011 00:00:00 +1400 (+14)
  auto tp = convert(civil_second(2011, 12, 29, 23, 59, 59), tz);
  ExpectTime(tp, tz, 2011, 12, 29, 23, 59, 59, -10 * 3600, true, "-10");
  EXPECT_EQ(363, get_yearday(convert(tp, tz)));
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 2011, 12, 31, 0, 0, 0, 14 * 3600, true, "+14");
  EXPECT_EQ(365, get_yearday(convert(tp, tz)));
}

TEST(TimeZoneEdgeCase, AfricaCairo) {
  const time_zone tz = LoadZone("Africa/Cairo");

  if (VersionCmp(tz, "2014c") >= 0) {
    // An interesting case of midnight not existing.
    //
    //   1400191199 == Thu, 15 May 2014 23:59:59 +0200 (EET)
    //   1400191200 == Fri, 16 May 2014 01:00:00 +0300 (EEST)
    auto tp = convert(civil_second(2014, 5, 15, 23, 59, 59), tz);
    ExpectTime(tp, tz, 2014, 5, 15, 23, 59, 59, 2 * 3600, false, "EET");
    tp += absl::time_internal::cctz::seconds(1);
    ExpectTime(tp, tz, 2014, 5, 16, 1, 0, 0, 3 * 3600, true, "EEST");
  }
}

TEST(TimeZoneEdgeCase, AfricaMonrovia) {
  const time_zone tz = LoadZone("Africa/Monrovia");

  if (VersionCmp(tz, "2017b") >= 0) {
    // Strange offset change -00:44:30 -> +00:00:00 (non-DST)
    //
    //   63593069 == Thu,  6 Jan 1972 23:59:59 -0044 (MMT)
    //   63593070 == Fri,  7 Jan 1972 00:44:30 +0000 (GMT)
    auto tp = convert(civil_second(1972, 1, 6, 23, 59, 59), tz);
    ExpectTime(tp, tz, 1972, 1, 6, 23, 59, 59, -44.5 * 60, false, "MMT");
    tp += absl::time_internal::cctz::seconds(1);
    ExpectTime(tp, tz, 1972, 1, 7, 0, 44, 30, 0 * 60, false, "GMT");
  }
}

TEST(TimeZoneEdgeCase, AmericaJamaica) {
  // Jamaica discontinued DST transitions in 1983, and is now at a
  // constant -0500.  This makes it an interesting edge-case target.
  // Note that the 32-bit times used in a (tzh_version == 0) zoneinfo
  // file cannot represent the abbreviation-only transition of 1890,
  // so we ignore the abbreviation by expecting what we received.
  const time_zone tz = LoadZone("America/Jamaica");

  // Before the first transition.
  if (!tz.version().empty() && VersionCmp(tz, "2018d") >= 0) {
    // We avoid the expectations on the -18430 offset below unless we are
    // certain we have commit 907241e (Fix off-by-1 error for Jamaica and
    // T&C before 1913) from 2018d.  TODO: Remove the "version() not empty"
    // part when 2018d is generally available from /usr/share/zoneinfo.
    auto tp = convert(civil_second(1889, 12, 31, 0, 0, 0), tz);
    ExpectTime(tp, tz, 1889, 12, 31, 0, 0, 0, -18430, false,
               tz.lookup(tp).abbr);

    // Over the first (abbreviation-change only) transition.
    //   -2524503170 == Tue, 31 Dec 1889 23:59:59 -0507 (LMT)
    //   -2524503169 == Wed,  1 Jan 1890 00:00:00 -0507 (KMT)
    tp = convert(civil_second(1889, 12, 31, 23, 59, 59), tz);
    ExpectTime(tp, tz, 1889, 12, 31, 23, 59, 59, -18430, false,
               tz.lookup(tp).abbr);
    tp += absl::time_internal::cctz::seconds(1);
    ExpectTime(tp, tz, 1890, 1, 1, 0, 0, 0, -18430, false, "KMT");
  }

  // Over the last (DST) transition.
  //     436341599 == Sun, 30 Oct 1983 01:59:59 -0400 (EDT)
  //     436341600 == Sun, 30 Oct 1983 01:00:00 -0500 (EST)
  auto tp = convert(civil_second(1983, 10, 30, 1, 59, 59), tz);
  ExpectTime(tp, tz, 1983, 10, 30, 1, 59, 59, -4 * 3600, true, "EDT");
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 1983, 10, 30, 1, 0, 0, -5 * 3600, false, "EST");

  // After the last transition.
  tp = convert(civil_second(1983, 12, 31, 23, 59, 59), tz);
  ExpectTime(tp, tz, 1983, 12, 31, 23, 59, 59, -5 * 3600, false, "EST");
}

TEST(TimeZoneEdgeCase, EuropeLisbon) {
  // Cover a non-existent time within a forward transition.
  const time_zone tz = LoadZone("Europe/Lisbon");

  // Over a forward transition.
  //     354671999 == Sat, 28 Mar 1981 23:59:59 +0000 (WET)
  //     354672000 == Sun, 29 Mar 1981 01:00:00 +0100 (WEST)
  auto tp = convert(civil_second(1981, 3, 28, 23, 59, 59), tz);
  ExpectTime(tp, tz, 1981, 3, 28, 23, 59, 59, 0, false, "WET");
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 1981, 3, 29, 1, 0, 0, 1 * 3600, true, "WEST");

  // A non-existent time within the transition.
  time_zone::civil_lookup cl1 = tz.lookup(civil_second(1981, 3, 29, 0, 15, 0));
  EXPECT_EQ(time_zone::civil_lookup::SKIPPED, cl1.kind);
  ExpectTime(cl1.pre, tz, 1981, 3, 29, 1, 15, 0, 1 * 3600, true, "WEST");
  ExpectTime(cl1.trans, tz, 1981, 3, 29, 1, 0, 0, 1 * 3600, true, "WEST");
  ExpectTime(cl1.post, tz, 1981, 3, 28, 23, 15, 0, 0 * 3600, false, "WET");
}

TEST(TimeZoneEdgeCase, FixedOffsets) {
  const time_zone gmtm5 = LoadZone("Etc/GMT+5");  // -0500
  auto tp = convert(civil_second(1970, 1, 1, 0, 0, 0), gmtm5);
  ExpectTime(tp, gmtm5, 1970, 1, 1, 0, 0, 0, -5 * 3600, false, "-05");
  EXPECT_EQ(chrono::system_clock::from_time_t(5 * 3600), tp);

  const time_zone gmtp5 = LoadZone("Etc/GMT-5");  // +0500
  tp = convert(civil_second(1970, 1, 1, 0, 0, 0), gmtp5);
  ExpectTime(tp, gmtp5, 1970, 1, 1, 0, 0, 0, 5 * 3600, false, "+05");
  EXPECT_EQ(chrono::system_clock::from_time_t(-5 * 3600), tp);
}

TEST(TimeZoneEdgeCase, NegativeYear) {
  // Tests transition from year 0 (aka 1BCE) to year -1.
  const time_zone tz = utc_time_zone();
  auto tp = convert(civil_second(0, 1, 1, 0, 0, 0), tz);
  ExpectTime(tp, tz, 0, 1, 1, 0, 0, 0, 0 * 3600, false, "UTC");
  EXPECT_EQ(weekday::saturday, get_weekday(convert(tp, tz)));
  tp -= absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, -1, 12, 31, 23, 59, 59, 0 * 3600, false, "UTC");
  EXPECT_EQ(weekday::friday, get_weekday(convert(tp, tz)));
}

TEST(TimeZoneEdgeCase, UTC32bitLimit) {
  const time_zone tz = utc_time_zone();

  // Limits of signed 32-bit time_t
  //
  //   2147483647 == Tue, 19 Jan 2038 03:14:07 +0000 (UTC)
  //   2147483648 == Tue, 19 Jan 2038 03:14:08 +0000 (UTC)
  auto tp = convert(civil_second(2038, 1, 19, 3, 14, 7), tz);
  ExpectTime(tp, tz, 2038, 1, 19, 3, 14, 7, 0 * 3600, false, "UTC");
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 2038, 1, 19, 3, 14, 8, 0 * 3600, false, "UTC");
}

TEST(TimeZoneEdgeCase, UTC5DigitYear) {
  const time_zone tz = utc_time_zone();

  // Rollover to 5-digit year
  //
  //   253402300799 == Fri, 31 Dec 9999 23:59:59 +0000 (UTC)
  //   253402300800 == Sat,  1 Jan 1000 00:00:00 +0000 (UTC)
  auto tp = convert(civil_second(9999, 12, 31, 23, 59, 59), tz);
  ExpectTime(tp, tz, 9999, 12, 31, 23, 59, 59, 0 * 3600, false, "UTC");
  tp += absl::time_internal::cctz::seconds(1);
  ExpectTime(tp, tz, 10000, 1, 1, 0, 0, 0, 0 * 3600, false, "UTC");
}

}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl
