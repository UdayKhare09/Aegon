#include "RedisLock.h"
#include "RedisClient.h"

namespace aegon::data::redis {

core::Task<bool> RedisLock::release() {
    if (!locked_ || !client_) {
        co_return false;
    }
    static constexpr std::string_view kReleaseScript =
        "if redis.call('GET', KEYS[1]) == ARGV[1] then "
        "return redis.call('DEL', KEYS[1]) "
        "else return 0 end";

    std::vector<std::string_view> keys{key_};
    std::vector<std::string_view> args{token_};
    auto resp = co_await client_->eval_script(kReleaseScript, keys, args);
    locked_ = false;
    if (resp.is_integer()) {
        co_return resp.as_integer() == 1;
    }
    co_return false;
}

core::Task<bool> RedisLock::extend(std::chrono::milliseconds extra_ttl) {
    if (!locked_ || !client_) {
        co_return false;
    }
    static constexpr std::string_view kExtendScript =
        "if redis.call('GET', KEYS[1]) == ARGV[1] then "
        "return redis.call('PEXPIRE', KEYS[1], ARGV[2]) "
        "else return 0 end";

    std::string ttl_str = std::to_string(extra_ttl.count());
    std::vector<std::string_view> keys{key_};
    std::vector<std::string_view> args{token_, ttl_str};
    auto resp = co_await client_->eval_script(kExtendScript, keys, args);
    if (resp.is_integer() && resp.as_integer() == 1) {
        ttl_ = extra_ttl;
        co_return true;
    }
    co_return false;
}

} // namespace aegon::data::redis
