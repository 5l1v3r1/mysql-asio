#ifndef INCLUDE_BOOST_MYSQL_DETAIL_PROTOCOL_IMPL_BINARY_SERIALIZATION_IPP_
#define INCLUDE_BOOST_MYSQL_DETAIL_PROTOCOL_IMPL_BINARY_SERIALIZATION_IPP_

#include <type_traits>
#include "boost/mysql/detail/protocol/float_serialization.hpp"

namespace boost {
namespace mysql {
namespace detail {

// Performs a mapping from T to a type that can be serialized
template <typename T>
struct get_serializable_type { using type = T; };

template <typename T>
using get_serializable_type_t = typename get_serializable_type<T>::type;

template <> struct get_serializable_type<std::uint32_t> { using type = int4; };
template <> struct get_serializable_type<std::int32_t> { using type = int4_signed; };
template <> struct get_serializable_type<std::uint64_t> { using type = int8; };
template <> struct get_serializable_type<std::int64_t> { using type = int8_signed; };
template <> struct get_serializable_type<std::string_view> { using type = string_lenenc; };
template <> struct get_serializable_type<std::nullptr_t> { using type = dummy_serializable; };

template <typename T>
inline get_serializable_type_t<T> to_serializable_type(T input) noexcept
{
	return get_serializable_type_t<T>(input);
}


inline errc deserialize_binary_date(date& output, std::uint8_t length, deserialization_context& ctx) noexcept
{
	int2 year;
	int1 month;
	int1 day;

	if (length >= 4) // if length is zero, year, month and day are zero
	{
		auto err = deserialize_fields(ctx, year, month, day);
		if (err != errc::ok) return err;
	}

	// TODO: how does this handle zero dates?
	::date::year_month_day ymd (::date::year(year.value), ::date::month(month.value), ::date::day(day.value));
	output = date(ymd);
	return errc::ok;
}

// Does not add the length prefix byte
inline void serialize_binary_ymd(
	const ::date::year_month_day& ymd,
	serialization_context& ctx
) noexcept
{
	serialize_fields(
		ctx,
		int2(static_cast<int>(ymd.year())),
		int1(static_cast<unsigned>(ymd.month())),
		int1(static_cast<unsigned>(ymd.day()))
	);
}

struct broken_datetime
{
	::date::year_month_day ymd;
	::date::time_of_day<std::chrono::microseconds> tod;

	broken_datetime(const datetime& input) noexcept :
		ymd(::date::floor<::date::days>(input)),
		tod(input - ::date::floor<::date::days>(input))
	{
	}

	// Doesn't count the first length byte
	std::uint8_t binary_serialized_length() const noexcept
	{
		std::uint8_t res = 11; // base length
		if (tod.subseconds().count() == 0)
		{
			res -= 4;
			if (tod.seconds().count() == 0 &&
			    tod.minutes().count() == 0 &&
				tod.hours().count() == 0)
			{
				res -= 3;
			}
		}
		return res;
	}
};

struct broken_time
{
	::date::days days;
	std::chrono::hours hours;
	std::chrono::minutes minutes;
	std::chrono::seconds seconds;
	std::chrono::microseconds microseconds;

	broken_time(const time& input) noexcept :
		days(std::chrono::duration_cast<::date::days>(input)),
		hours(std::chrono::duration_cast<std::chrono::hours>(input % ::date::days(1))),
		minutes(std::chrono::duration_cast<std::chrono::minutes>(input % std::chrono::hours(1))),
		seconds(std::chrono::duration_cast<std::chrono::seconds>(input % std::chrono::minutes(1))),
		microseconds(input % std::chrono::seconds(1))
	{
	}

	// Doesn't count the first length byte
	std::uint8_t binary_serialized_length() const noexcept
	{
		std::uint8_t res = 12;
		if (microseconds.count() == 0)
		{
			res -= 4;
			if (seconds.count() == 0 &&
				minutes.count() == 0 &&
				hours.count() == 0 &&
				days.count() == 0)
			{
				res -= 8;
			}
		}
		return res;
	}
};

} // detail
} // mysql
} // boost

// date
inline std::size_t boost::mysql::detail::get_size(
	const date&,
	const serialization_context&
) noexcept
{
	// TODO: consider zero dates?
	return 5; // length, year, month, day
}

inline void boost::mysql::detail::serialize(
	const date& input,
	serialization_context& ctx
) noexcept
{
	// TODO: consider zero dates?
	serialize(int1(4), ctx); //
	serialize_binary_ymd(::date::year_month_day (input), ctx);
}

inline boost::mysql::errc boost::mysql::detail::deserialize(
	date& output,
	deserialization_context& ctx
) noexcept
{
	int1 length;
	auto err = deserialize(length, ctx);
	if (err != errc::ok) return err;
	return deserialize_binary_date(output, length.value, ctx);
}

// datetime
inline std::size_t boost::mysql::detail::get_size(
	const datetime& input,
	const serialization_context&
) noexcept
{
	broken_datetime dt (input);
	return dt.binary_serialized_length() + 1; // extra length prefix byte
}

inline void boost::mysql::detail::serialize(
	const datetime& input,
	serialization_context& ctx
) noexcept
{
	broken_datetime brokendt (input);
	auto length = brokendt.binary_serialized_length();
	serialize(int1(length), ctx);
	if (length >= 4) // TODO: refactor these magic constants
	{
		serialize_binary_ymd(brokendt.ymd, ctx);
	}
	if (length >= 7)
	{
		serialize_fields(
			ctx,
			int1(static_cast<std::uint8_t>(brokendt.tod.hours().count())),
			int1(static_cast<std::uint8_t>(brokendt.tod.minutes().count())),
			int1(static_cast<std::uint8_t>(brokendt.tod.seconds().count()))
		);
	}
	if (length >= 11)
	{
		auto micros = static_cast<std::uint32_t>(brokendt.tod.subseconds().count());
		serialize(int4(micros), ctx);
	}
}

inline boost::mysql::errc boost::mysql::detail::deserialize(
	datetime& output,
	deserialization_context& ctx
) noexcept
{
	int1 length;
	date date_part;
	int1 hours;
	int1 minutes;
	int1 seconds;
	int4 micros;

	// Deserialize length
	auto err = deserialize(length, ctx);
	if (err != errc::ok) return err;

	// Based on length, deserialize the rest of the fields
	err = deserialize_binary_date(date_part, length.value, ctx);
	if (err != errc::ok) return err;
	if (length.value >= 7)
	{
		err = deserialize_fields(ctx, hours, minutes, seconds);
		if (err != errc::ok) return err;
	}
	if (length.value >= 11)
	{
		err = deserialize(micros, ctx);
		if (err != errc::ok) return err;
	}

	// Compose the final datetime. Doing time of day and date separately to avoid overflow
	auto time_of_day_part = std::chrono::hours(hours.value) + std::chrono::minutes(minutes.value) +
		std::chrono::seconds(seconds.value) + std::chrono::microseconds(micros.value);
	output = date_part + time_of_day_part;
	return errc::ok;
}

// time
inline std::size_t boost::mysql::detail::get_size(
	const time& input,
	const serialization_context&
) noexcept
{
	return broken_time(input).binary_serialized_length() + 1; // length byte
}

inline void boost::mysql::detail::serialize(
	const time& input,
	serialization_context& ctx
) noexcept
{
	broken_time broken (input);
	auto length = broken.binary_serialized_length();
	serialize(int1(length), ctx);
	if (length >= 8) // TODO: magic constants
	{
		int1 is_negative (input.count() < 0 ? 1 : 0);
		serialize_fields(
			ctx,
			is_negative,
			int4(static_cast<std::uint32_t>(std::abs(broken.days.count()))),
			int1(static_cast<std::uint8_t>(std::abs(broken.hours.count()))),
			int1(static_cast<std::uint8_t>(std::abs(broken.minutes.count()))),
			int1(static_cast<std::uint8_t>(std::abs(broken.seconds.count())))
		);
	}
	if (length >= 12)
	{
		auto micros = static_cast<std::uint32_t>(std::abs(broken.microseconds.count()));
		serialize(int4(micros), ctx);
	}
}

inline boost::mysql::errc boost::mysql::detail::deserialize(
	time& output,
	deserialization_context& ctx
) noexcept
{
	// Length
	int1 length;
	auto err = deserialize(length, ctx);
	if (err != errc::ok) return err;

	int1 is_negative (0);
	int4 days (0);
	int1 hours (0);
	int1 minutes(0);
	int1 seconds(0);
	int4 microseconds(0);

	if (length.value >= 8)
	{
		err = deserialize_fields(
			ctx,
			is_negative,
			days,
			hours,
			minutes,
			seconds
		);
		if (err != errc::ok) return err;
	}
	if (length.value >= 12)
	{
		err = deserialize(microseconds, ctx);
		if (err != errc::ok) return err;
	}

	output = (is_negative.value ? -1 : 1) * (
		 ::date::days(days.value) +
		 std::chrono::hours(hours.value) +
		 std::chrono::minutes(minutes.value) +
		 std::chrono::seconds(seconds.value) +
		 std::chrono::microseconds(microseconds.value)
	);
	return errc::ok;
}

// mysql::value
inline std::size_t boost::mysql::detail::get_size(
	const value& input,
	const serialization_context& ctx
) noexcept
{
	return std::visit([&ctx](const auto& v) {
		return get_size(to_serializable_type(v), ctx);
	}, input);
}

inline void boost::mysql::detail::serialize(
	const value& input,
	serialization_context& ctx
) noexcept
{
	std::visit([&ctx](const auto& v) {
		serialize(to_serializable_type(v), ctx);
	}, input);
}



#endif /* INCLUDE_BOOST_MYSQL_DETAIL_PROTOCOL_IMPL_BINARY_SERIALIZATION_IPP_ */
