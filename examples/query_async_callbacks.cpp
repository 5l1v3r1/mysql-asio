
#include "boost/mysql/connection.hpp"
#include <boost/asio/io_service.hpp>
#include <boost/system/system_error.hpp>
#include <boost/asio/coroutine.hpp>
#include <boost/asio/yield.hpp>
#include <iostream>

using boost::mysql::error_code;
using boost::mysql::error_info;
using boost::mysql::tcp_resultset;
using boost::mysql::owning_row;

/**
 * For this example, we will be using the 'mysql_asio_examples' database.
 * You can get this database by running db_setup.sql.
 * This example assumes you are connecting to a localhost MySQL server.
 *
 * This example uses asynchronous functions with callbacks.
 *
 * This example assumes you are already familiar with the basic concepts
 * of mysql-asio (tcp_connection, resultset, rows, values). If you are not,
 * please have a look to the query_sync.cpp example.
 *
 * In this library, all asynchronous operations follow Boost.Asio universal
 * asynchronous models, and thus may be used with callbacks, coroutines or futures.
 * The handler signature is always one of:
 *   - void(error_code): for operations that do not have a "return type" (e.g. handshake)
 *   - void(error_code, T): for operations that have a "return type" (e.g. query, for which
 *     T = resultset<StreamType>).
 *
 * All asynchronous operations accept a last optional error_info* parameter. error_info
 * contains additional diagnostic information returned by the server. If you
 * pass a non-nullptr value, it will be populated in case of error if any extra information
 * is available.
 *
 * Design note: handler signatures in Boost.Asio should have two parameters, at
 * most, and the first one should be an error_code - otherwise some of the asynchronous
 * features (e.g. coroutines) won't work. This is why error_info is not part of any
 * of the handler signatures.
 */

void print_employee(const boost::mysql::row& employee)
{
	using boost::mysql::operator<<; // Required for mysql::value objects to be streamable, due to ADL rules
	std::cout << "Employee '"
			  << employee.values()[0] << " "                   // first_name (type std::string_view)
			  << employee.values()[1] << "' earns "            // last_name  (type std::string_view)
			  << employee.values()[2] << " dollars yearly\n";  // salary     (type double)
}

void die_on_error(
	const error_code& err,
	const boost::mysql::error_info& info = boost::mysql::error_info()
)
{
	if (err)
	{
		std::cerr << "Error: " << err << ": " << info.message() << std::endl;
		exit(1);
	}
}

class application
{
	boost::asio::ip::tcp::endpoint ep;            // Physical endpoint to connect to
	boost::mysql::connection_params conn_params;  // MySQL credentials and other connection config
	boost::asio::io_context ctx;                  // boost::asio context
	boost::mysql::tcp_connection connection;      // Represents the connection to the MySQL server
	boost::mysql::tcp_resultset resultset;        // A result from a query
	boost::mysql::error_info additional_info;     // Will be populated with additional information about any errors
public:
	application(const char* username, const char* password) :
		ep (boost::asio::ip::address_v4::loopback(), boost::mysql::default_port),
		conn_params(username, password, "mysql_asio_examples"),
		connection(ctx)
	{
	}

	void start() { connect(); }

	void connect()
	{
		connection.next_layer().async_connect(ep, [this](error_code err) {
			die_on_error(err);
			connection.async_handshake(conn_params, [this](error_code err) {
				die_on_error(err, additional_info);
				query_employees();
			}, &additional_info);
		});
	}

	void query_employees()
	{
		const char* sql = "SELECT first_name, last_name, salary FROM employee WHERE company_id = 'HGS'";
		connection.async_query(sql, [this](error_code err, tcp_resultset&& result) {
			die_on_error(err, additional_info);
			resultset = std::move(result);
			resultset.async_fetch_all([this](error_code err, const std::vector<owning_row>& rows) {
				die_on_error(err, additional_info);
				for (const auto& employee: rows)
				{
					print_employee(employee);
				}
				update_slacker();
			}, &additional_info);
		}, &additional_info);
	}

	void update_slacker()
	{
		const char* sql = "UPDATE employee SET salary = 15000 WHERE last_name = 'Slacker'";
		connection.async_query(sql, [this](error_code err, [[maybe_unused]] tcp_resultset&& result) {
			die_on_error(err, additional_info);
			assert(result.fields().size() == 0);
			query_intern();
		}, &additional_info);
	}

	void query_intern()
	{
		const char* sql = "SELECT salary FROM employee WHERE last_name = 'Slacker'";
		connection.async_query(sql, [this](error_code err, tcp_resultset&& result) {
			die_on_error(err, additional_info);
			resultset = std::move(result);
			resultset.async_fetch_all([this](error_code err, const std::vector<owning_row>& rows) {
				die_on_error(err, additional_info);
				assert(rows.size() == 1);
				[[maybe_unused]] auto salary = std::get<double>(rows[0].values()[0]);
				assert(salary == 15000);
			}, &additional_info);
		}, &additional_info);
	}

	auto& context() { return ctx; }
};

void main_impl(int argc, char** argv)
{
	if (argc != 3)
	{
		std::cerr << "Usage: " << argv[0] << " <username> <password>\n";
		exit(1);
	}

	application app (argv[1], argv[2]);
	app.start(); // starts the async chain
	app.context().run(); // run the asio::io_context until the async chain finishes
}

int main(int argc, char** argv)
{
	try
	{
		main_impl(argc, argv);
	}
	catch (const boost::system::system_error& err)
	{
		std::cerr << "Error: " << err.what() << ", error code: " << err.code() << std::endl;
		return 1;
	}
	catch (const std::exception& err)
	{
		std::cerr << "Error: " << err.what() << std::endl;
		return 1;
	}
}
