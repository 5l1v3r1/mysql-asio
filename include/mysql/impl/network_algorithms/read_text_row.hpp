#ifndef INCLUDE_MYSQL_IMPL_NETWORK_ALGORITHMS_READ_TEXT_ROW_HPP_
#define INCLUDE_MYSQL_IMPL_NETWORK_ALGORITHMS_READ_TEXT_ROW_HPP_

#include "mysql/impl/network_algorithms/common.hpp"
#include "mysql/impl/basic_types.hpp"
#include "mysql/impl/messages.hpp"
#include "mysql/error.hpp"
#include <string_view>
#include <vector>

namespace mysql
{
namespace detail
{

template <typename ChannelType>
fetch_result fetch_text_row(
	ChannelType& channel,
	const std::vector<field_metadata>& meta,
	bytestring& buffer,
	std::vector<value>& output_values,
	ok_packet& output_ok_packet,
	error_code& err,
	error_info& info
);

template <typename ChannelType, typename CompletionToken>
BOOST_ASIO_INITFN_RESULT_TYPE(CompletionToken, void(error_code, error_info, fetch_result))
async_fetch_text_row(
	ChannelType& channel,
	const std::vector<field_metadata>& meta,
	bytestring& buffer,
	std::vector<value>& output_values,
	ok_packet& output_ok_packet,
	CompletionToken&& token
);


}
}

#include "mysql/impl/network_algorithms/read_text_row.ipp"



#endif /* INCLUDE_MYSQL_IMPL_NETWORK_ALGORITHMS_READ_TEXT_ROW_HPP_ */
