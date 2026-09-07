#pragma once

// Umbrella header for all Aegon foundational data types
#include "data/types/UUID.h"
#include "data/types/DateTime.h"
#include "data/types/Date.h"
#include "data/types/Time.h"
#include "data/types/Decimal.h"
#include "data/types/Json.h"
#include "data/types/IpAddress.h"
#include "data/types/MacAddress.h"
#include "data/types/Blob.h"
#include "data/types/Hash256.h"

namespace aegon::data {

using types::UUID;
using types::UUIDGenerator;
using types::DateTime;
using types::Date;
using types::Time;
using types::Decimal;
using types::Decimal128;
using types::Json;
using types::IpAddress;
using types::MacAddress;
using types::Blob;
using types::Hash256;

} // namespace aegon::data
