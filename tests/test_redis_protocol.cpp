#include "data/redis/Resp3.h"
#include "data/redis/Crc16.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace aegon::data::redis;

void test_resp3_serialization() {
    std::vector<std::string_view> cmd = {"SET", "my_key", "hello world"};
    std::string wire = Resp3Serializer::serialize_command(cmd);
    std::string expected = "*3\r\n$3\r\nSET\r\n$6\r\nmy_key\r\n$11\r\nhello world\r\n";
    assert(wire == expected);
    std::cout << "[PASS] test_resp3_serialization\n";
}

void test_resp3_parsing() {
    // 1. Simple String
    {
        std::string_view in = "+OK\r\n";
        RespValue val;
        assert(Resp3Parser::parse(in, val) == ParseStatus::Done);
        assert(val.is_string());
        assert(val.as_string() == "OK");
        assert(in.empty());
    }

    // 2. Error
    {
        std::string_view in = "-MOVED 3999 127.0.0.1:6381\r\n";
        RespValue val;
        assert(Resp3Parser::parse(in, val) == ParseStatus::Done);
        assert(val.is_error());
        assert(val.as_string() == "MOVED 3999 127.0.0.1:6381");
        assert(in.empty());
    }

    // 3. Integer
    {
        std::string_view in = ":1048576\r\n";
        RespValue val;
        assert(Resp3Parser::parse(in, val) == ParseStatus::Done);
        assert(val.is_integer());
        assert(val.as_integer() == 1048576);
        assert(in.empty());
    }

    // 4. Bulk String
    {
        std::string_view in = "$12\r\nhello aegon!\r\n";
        RespValue val;
        assert(Resp3Parser::parse(in, val) == ParseStatus::Done);
        assert(val.is_string());
        assert(val.as_string() == "hello aegon!");
        assert(in.empty());
    }

    // 5. Null Bulk String
    {
        std::string_view in = "$-1\r\n";
        RespValue val;
        assert(Resp3Parser::parse(in, val) == ParseStatus::Done);
        assert(val.is_null());
        assert(in.empty());
    }

    // 6. RESP3 Null
    {
        std::string_view in = "_\r\n";
        RespValue val;
        assert(Resp3Parser::parse(in, val) == ParseStatus::Done);
        assert(val.is_null());
        assert(in.empty());
    }

    // 7. Boolean
    {
        std::string_view in_t = "#t\r\n";
        RespValue val_t;
        assert(Resp3Parser::parse(in_t, val_t) == ParseStatus::Done);
        assert(std::get<bool>(val_t.data) == true);

        std::string_view in_f = "#f\r\n";
        RespValue val_f;
        assert(Resp3Parser::parse(in_f, val_f) == ParseStatus::Done);
        assert(std::get<bool>(val_f.data) == false);
    }

    // 8. Array
    {
        std::string_view in = "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
        RespValue val;
        assert(Resp3Parser::parse(in, val) == ParseStatus::Done);
        assert(val.is_array());
        const auto& arr = val.as_array();
        assert(arr.size() == 2);
        assert(arr[0].as_string() == "foo");
        assert(arr[1].as_string() == "bar");
        assert(in.empty());
    }

    // 9. Map (%2\r\n+k1\r\n:10\r\n+k2\r\n:20\r\n)
    {
        std::string_view in = "%2\r\n+k1\r\n:10\r\n+k2\r\n:20\r\n";
        RespValue val;
        assert(Resp3Parser::parse(in, val) == ParseStatus::Done);
        assert(val.is_array());
        const auto& arr = val.as_array();
        assert(arr.size() == 4);
        assert(arr[0].as_string() == "k1");
        assert(arr[1].as_integer() == 10);
        assert(arr[2].as_string() == "k2");
        assert(arr[3].as_integer() == 20);
        assert(in.empty());
    }

    // 10. Double (,3.14159\r\n)
    {
        std::string_view in = ",3.14159\r\n";
        RespValue val;
        assert(Resp3Parser::parse(in, val) == ParseStatus::Done);
        assert(val.is_double());
        assert(std::abs(val.as_double() - 3.14159) < 1e-4);
        assert(in.empty());
    }

    // 11. Incremental byte-by-byte streaming reassembly
    {
        std::string full_wire = "*3\r\n$3\r\nfoo\r\n:42\r\n$5\r\nhello\r\n";
        std::string stream_buf;
        RespValue val;

        // Feed byte-by-byte until the last byte
        for (size_t i = 0; i < full_wire.size() - 1; ++i) {
            stream_buf.push_back(full_wire[i]);
            std::string_view view = stream_buf;
            ParseStatus st = Resp3Parser::parse(view, val);
            assert(st == ParseStatus::NeedMoreData);
        }

        // Feed final byte
        stream_buf.push_back(full_wire.back());
        std::string_view view = stream_buf;
        ParseStatus st = Resp3Parser::parse(view, val);
        assert(st == ParseStatus::Done);
        assert(val.is_array());
        assert(val.as_array().size() == 3);
        assert(val.as_array()[0].as_string() == "foo");
        assert(val.as_array()[1].as_integer() == 42);
        assert(val.as_array()[2].as_string() == "hello");
        assert(view.empty());
    }

    std::cout << "[PASS] test_resp3_parsing (including streaming fragment reassembly)\n";
}

void test_crc16_and_cluster_slots() {
    // Standard XMODEM check vector for "123456789" is 0x31C3
    uint16_t c = crc16("123456789");
    assert(c == 0x31C3);

    // Hash slots
    uint16_t slot1 = key_slot("user:101");
    assert(slot1 < 16384);

    // Hash tags: {user:101}:orders and {user:101}:profile must map to the exact same slot!
    uint16_t slot_orders = key_slot("{user:101}:orders");
    uint16_t slot_profile = key_slot("{user:101}:profile");
    uint16_t slot_user = key_slot("user:101");

    assert(slot_orders == slot_profile);
    assert(slot_orders == slot_user);

    std::cout << "[PASS] test_crc16_and_cluster_slots\n";
}

int main() {
    std::cout << "Running Redis Protocol Tests...\n";
    test_resp3_serialization();
    test_resp3_parsing();
    test_crc16_and_cluster_slots();
    std::cout << "All Redis Protocol Tests PASSED successfully!\n";
    return 0;
}
