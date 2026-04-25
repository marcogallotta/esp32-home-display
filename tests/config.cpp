#include "config.h"
#include "helpers.h"

#include "doctest/doctest.h"

namespace {

std::string validConfigJson() {
    return R"json(
{
    "forecast": {
        "openmeteo_pem": "-----BEGIN CERTIFICATE-----\nTEST\n-----END CERTIFICATE-----\n",
        "update_interval_minutes": 30
    },
    "location": {
        "latitude": 41.9,
        "longitude": 12.5,
        "timezone": "Europe/Rome",
        "timezone_long": "CET-1CEST,M3.5.0/2,M10.5.0/3"
    },
    "salah": {
        "timezone_offset_minutes": 60,
        "dst_rule": "eu",
        "asr_makruh_minutes": 120,
        "hanafi_asr": true
    },
    "switchbot": {
        "sensors": [
            {
                "mac": "AA:BB:CC:DD:EE:FF",
                "name": "Living Room",
                "short_name": "LR"
            },
            {
                "mac": "11:22:33:44:55:66",
                "name": "Bedroom",
                "short_name": "BR"
            }
        ]
    },
    "xiaomi": {
        "update_interval_minutes": 60,
        "sensors": [
            {
                "mac": "AA:AA:AA:AA:AA:AA",
                "name": "Plant 1",
                "short_name": "P1"
            },
            {
                "mac": "BB:BB:BB:BB:BB:BB",
                "name": "Plant 2",
                "short_name": "P2"
            }
        ]
    },
    "wifi": {
        "ssid": "TestWifi",
        "password": "Secret123"
    }
}
)json";
}

TEST_CASE("testParseValidConfig") {
    Config config;
    const bool ok = parseConfigText(validConfigJson(), config, false);

    assertTrue(ok, "valid config should parse");

    assertEqual(config.forecast.openmeteoPem,
                std::string("-----BEGIN CERTIFICATE-----\nTEST\n-----END CERTIFICATE-----\n"),
                "forecast pem should match");
    assertEqual(config.forecast.updateIntervalMinutes, 30,
                "forecast interval should match");

    assertEqual(config.location.latitude, 41.9f,
                "latitude should match");
    assertEqual(config.location.longitude, 12.5f,
                "longitude should match");
    assertEqual(config.location.timezone, std::string("Europe/Rome"),
                "timezone should match");
    assertEqual(config.location.timezoneLong, std::string("CET-1CEST,M3.5.0/2,M10.5.0/3"),
                "timezone_long should match");

    assertEqual(config.salah.timezoneOffsetMinutes, 60,
                "salah timezone offset should match");
    assertEqual(config.salah.dstRule, std::string("eu"),
                "dst rule should match");
    assertEqual(config.salah.asrMakruhMinutes, 120,
                "asr makruh minutes should match");
    assertTrue(config.salah.hanafiAsr,
               "hanafi_asr should match");

    assertEqual(config.switchbot.sensors.size(), std::size_t(2),
                "sensor count should match");
    assertEqual(config.switchbot.sensors[0].mac, std::string("AA:BB:CC:DD:EE:FF"),
                "first sensor mac should match");
    assertEqual(config.switchbot.sensors[0].name, std::string("Living Room"),
                "first sensor name should match");
    assertEqual(config.switchbot.sensors[0].shortName, std::string("LR"),
                "first sensor short name should match");

    assertEqual(config.xiaomi.updateIntervalMinutes, 60,
                "xiaomi interval should match");
    assertEqual(config.xiaomi.sensors.size(), std::size_t(2),
                "xiaomi sensor count should match");
    assertEqual(config.xiaomi.sensors[0].mac, std::string("AA:AA:AA:AA:AA:AA"),
                "first xiaomi sensor mac should match");
    assertEqual(config.xiaomi.sensors[0].name, std::string("Plant 1"),
                "first xiaomi sensor name should match");
    assertEqual(config.xiaomi.sensors[0].shortName, std::string("P1"),
                "first xiaomi sensor short name should match");

    assertEqual(config.wifi.ssid, std::string("TestWifi"),
                "ssid should match");
    assertEqual(config.wifi.password, std::string("Secret123"),
                "password should match");
}

TEST_CASE("testMissingForecastFails") {
    const std::string text = R"json(
{
    "location": {
        "latitude": 41.9,
        "longitude": 12.5,
        "timezone": "Europe/Rome",
        "timezone_long": "CET-1CEST,M3.5.0/2,M10.5.0/3"
    },
    "salah": {
        "timezone_offset_minutes": 60,
        "dst_rule": "eu",
        "asr_makruh_minutes": 120,
        "hanafi_asr": true
    },
    "switchbot": {
        "sensors": []
    },
    "xiaomi": {
        "update_interval_minutes": 60,
        "sensors": []
    },
    "wifi": {
        "ssid": "TestWifi",
        "password": "Secret123"
    }
}
)json";

    Config config;
    const bool ok = parseConfigText(text, config, false);
    assertTrue(!ok, "missing forecast should fail");
}

TEST_CASE("testInvalidForecastIntervalFails") {
    const std::string text = R"json(
{
    "forecast": {
        "openmeteo_pem": "pem",
        "update_interval_minutes": 0
    },
    "location": {
        "latitude": 41.9,
        "longitude": 12.5,
        "timezone": "Europe/Rome",
        "timezone_long": "CET-1CEST,M3.5.0/2,M10.5.0/3"
    },
    "salah": {
        "timezone_offset_minutes": 60,
        "dst_rule": "eu",
        "asr_makruh_minutes": 120,
        "hanafi_asr": true
    },
    "switchbot": {
        "sensors": []
    },
    "xiaomi": {
        "update_interval_minutes": 60,
        "sensors": []
    },
    "wifi": {
        "ssid": "TestWifi",
        "password": "Secret123"
    }
}
)json";

    Config config;
    const bool ok = parseConfigText(text, config, false);
    assertTrue(!ok, "non-positive forecast interval should fail");
}

TEST_CASE("testInvalidLatitudeFails") {
    const std::string text = R"json(
{
    "forecast": {
        "openmeteo_pem": "pem",
        "update_interval_minutes": 30
    },
    "location": {
        "latitude": 200.0,
        "longitude": 12.5,
        "timezone": "Europe/Rome",
        "timezone_long": "CET-1CEST,M3.5.0/2,M10.5.0/3"
    },
    "salah": {
        "timezone_offset_minutes": 60,
        "dst_rule": "eu",
        "asr_makruh_minutes": 120,
        "hanafi_asr": true
    },
    "switchbot": {
        "sensors": []
    },
    "xiaomi": {
        "update_interval_minutes": 60,
        "sensors": []
    },
    "wifi": {
        "ssid": "TestWifi",
        "password": "Secret123"
    }
}
)json";

    Config config;
    const bool ok = parseConfigText(text, config, false);
    assertTrue(!ok, "out of range latitude should fail");
}

TEST_CASE("testUnsupportedDstRuleFails") {
    const std::string text = R"json(
{
    "forecast": {
        "openmeteo_pem": "pem",
        "update_interval_minutes": 30
    },
    "location": {
        "latitude": 41.9,
        "longitude": 12.5,
        "timezone": "Europe/Rome",
        "timezone_long": "CET-1CEST,M3.5.0/2,M10.5.0/3"
    },
    "salah": {
        "timezone_offset_minutes": 60,
        "dst_rule": "mars",
        "asr_makruh_minutes": 120,
        "hanafi_asr": true
    },
    "switchbot": {
        "sensors": []
    },
    "xiaomi": {
        "update_interval_minutes": 60,
        "sensors": []
    },
    "wifi": {
        "ssid": "TestWifi",
        "password": "Secret123"
    }
}
)json";

    Config config;
    const bool ok = parseConfigText(text, config, false);
    assertTrue(!ok, "unsupported dst rule should fail");
}

TEST_CASE("testNegativeAsrMakruhFails") {
    const std::string text = R"json(
{
    "forecast": {
        "openmeteo_pem": "pem",
        "update_interval_minutes": 30
    },
    "location": {
        "latitude": 41.9,
        "longitude": 12.5,
        "timezone": "Europe/Rome",
        "timezone_long": "CET-1CEST,M3.5.0/2,M10.5.0/3"
    },
    "salah": {
        "timezone_offset_minutes": 60,
        "dst_rule": "eu",
        "asr_makruh_minutes": -1,
        "hanafi_asr": true
    },
    "switchbot": {
        "sensors": []
    },
    "xiaomi": {
        "update_interval_minutes": 60,
        "sensors": []
    },
    "wifi": {
        "ssid": "TestWifi",
        "password": "Secret123"
    }
}
)json";

    Config config;
    const bool ok = parseConfigText(text, config, false);
    assertTrue(!ok, "negative asr makruh should fail");
}

TEST_CASE("testMissingSwitchbotSensorsFails") {
    const std::string text = R"json(
{
    "forecast": {
        "openmeteo_pem": "pem",
        "update_interval_minutes": 30
    },
    "location": {
        "latitude": 41.9,
        "longitude": 12.5,
        "timezone": "Europe/Rome",
        "timezone_long": "CET-1CEST,M3.5.0/2,M10.5.0/3"
    },
    "salah": {
        "timezone_offset_minutes": 60,
        "dst_rule": "eu",
        "asr_makruh_minutes": 120,
        "hanafi_asr": true
    },
    "switchbot": {},
    "xiaomi": {
        "update_interval_minutes": 60,
        "sensors": []
    },
    "wifi": {
        "ssid": "TestWifi",
        "password": "Secret123"
    }
}
)json";

    Config config;
    const bool ok = parseConfigText(text, config, false);
    assertTrue(!ok, "missing switchbot.sensors should fail");
}

TEST_CASE("testWrongSensorFieldTypesFail") {
    const std::string text = R"json(
{
    "forecast": {
        "openmeteo_pem": "pem",
        "update_interval_minutes": 30
    },
    "location": {
        "latitude": 41.9,
        "longitude": 12.5,
        "timezone": "Europe/Rome",
        "timezone_long": "CET-1CEST,M3.5.0/2,M10.5.0/3"
    },
    "salah": {
        "timezone_offset_minutes": 60,
        "dst_rule": "eu",
        "asr_makruh_minutes": 120,
        "hanafi_asr": true
    },
    "switchbot": {
        "sensors": [
            {
                "mac": 123,
                "name": true,
                "short_name": []
            }
        ]
    },
    "xiaomi": {
        "update_interval_minutes": 60,
        "sensors": []
    },
    "wifi": {
        "ssid": "TestWifi",
        "password": "Secret123"
    }
}
)json";

    Config config;
    const bool ok = parseConfigText(text, config, false);
    assertTrue(!ok, "wrong sensor field types should fail");
}

TEST_CASE("testMissingWifiFails") {
    const std::string text = R"json(
{
    "forecast": {
        "openmeteo_pem": "pem",
        "update_interval_minutes": 30
    },
    "location": {
        "latitude": 41.9,
        "longitude": 12.5,
        "timezone": "Europe/Rome",
        "timezone_long": "CET-1CEST,M3.5.0/2,M10.5.0/3"
    },
    "salah": {
        "timezone_offset_minutes": 60,
        "dst_rule": "eu",
        "asr_makruh_minutes": 120,
        "hanafi_asr": true
    },
    "switchbot": {
        "sensors": []
    },
    "xiaomi": {
        "update_interval_minutes": 60,
        "sensors": []
    }
}
)json";

    Config config;
    const bool ok = parseConfigText(text, config, false);
    assertTrue(!ok, "missing wifi should fail");
}

TEST_CASE("testInvalidJsonFails") {
    const std::string text = R"json(
{
    "forecast": {
        "openmeteo_pem": "pem",
        "update_interval_minutes": 30,
    }
}
)json";

    Config config;
    const bool ok = parseConfigText(text, config, false);
    assertTrue(!ok, "invalid json should fail");
}

TEST_CASE("testMissingXiaomiFails") {
    const std::string text = R"json(
{
    "forecast": {
        "openmeteo_pem": "pem",
        "update_interval_minutes": 30
    },
    "location": {
        "latitude": 41.9,
        "longitude": 12.5,
        "timezone": "Europe/Rome",
        "timezone_long": "CET-1CEST,M3.5.0/2,M10.5.0/3"
    },
    "salah": {
        "timezone_offset_minutes": 60,
        "dst_rule": "eu",
        "asr_makruh_minutes": 120,
        "hanafi_asr": true
    },
    "switchbot": {
        "sensors": []
    },
    "wifi": {
        "ssid": "TestWifi",
        "password": "Secret123"
    }
}
)json";

    Config config;
    const bool ok = parseConfigText(text, config, false);
    assertTrue(!ok, "missing xiaomi should fail");
}

} // namespace
