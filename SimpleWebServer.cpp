#include "server_http.hpp"
#include "client_http.hpp"

//Added for the json-example
#define BOOST_SPIRIT_THREADSAFE
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

//Added for the default_resource example
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <boost/filesystem.hpp>
#include <vector>
#include <algorithm>
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>

using namespace std;
//Added for the json-example:
using namespace boost::property_tree;

typedef SimpleWeb::Server<SimpleWeb::HTTP> HttpServer;
typedef SimpleWeb::Client<SimpleWeb::HTTP> HttpClient;

//Added for the default_resource example
void default_resource_send(const HttpServer &server, shared_ptr<HttpServer::Response> response,
                           shared_ptr<ifstream> ifs, shared_ptr<vector<char> > buffer);
string mysql_getAll();
string mysql_get_data(int id);
void mysql_set_data(ptree pt);
void mysql_update_data(ptree pt, int id);

int main() {
    //HTTP-server at port 8080 using 1 thread
    //Unless you do more heavy non-threaded processing in the resources,
    //1 thread is usually faster than several threads
    HttpServer server(8080, 1);

    //GET-example for the path /info
    //Responds with request-information
    server.resource["^/info$"]["GET"]=[](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
        stringstream content_stream;
        content_stream << "<h1>Request from " << request->remote_endpoint_address << " (" << request->remote_endpoint_port << ")</h1>";
        content_stream << request->method << " " << request->path << " HTTP/" << request->http_version << "<br>";
        for(auto& header: request->header) {
            content_stream << header.first << ": " << header.second << "<br>";
        }

        //find length of content_stream (length received using content_stream.tellp())
        content_stream.seekp(0, ios::end);

        *response <<  "HTTP/1.1 200 OK\r\nContent-Length: " << content_stream.tellp() << "\r\n\r\n" << content_stream.rdbuf();
    };

    //GET-example for the path /match/[number], responds with the matched string in path (number)
    //For instance a request GET /match/123 will receive: 123

 /*   //Get example simulating heavy work in a separate thread
    server.resource["^/work$"]["GET"]=[&server](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
        thread work_thread([response] {
            this_thread::sleep_for(chrono::seconds(5));
            string message="Work done";
            *response << "HTTP/1.1 200 OK\r\nContent-Length: " << message.length() << "\r\n\r\n" << message;
        });
        work_thread.detach();
    }; */

    //Get example simulating heavy work in a separate thread
    server.resource["^/mysql$"]["GET"]=[&server](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> /*request*/) {
        thread work_thread([response] {

        	string message = mysql_getAll();

        	//this_thread::sleep_for(chrono::seconds(3));

            *response << "HTTP/1.1 200 OK\r\nContent-Length: " << message.length() << "\r\n\r\n" << message;
        });
        work_thread.detach();
    };

    server.resource["^/mysql/([0-9]+)$"]["GET"]=[&server](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
          string number=request->path_match[1];

          string department = mysql_get_data(stoi(number));

          *response << "HTTP/1.1 200 OK\r\nContent-Length: " << department.length() << "\r\n\r\n" << department;
      };


    //POST-example for the path /json, responds firstName+" "+lastName from the posted json
    //Responds with an appropriate error message if the posted json is not valid, or if firstName or lastName is missing
    //Example posted json:
    //{
    //  "firstName": "John",
    //  "lastName": "Smith",
    //  "dept": "Tech"
    //}
    server.resource["^/mysql$"]["POST"]=[](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
           try {
               ptree pt;
               read_json(request->content, pt);
               mysql_set_data(pt);
               string message = "Data Written";

               *response << "HTTP/1.1 200 OK\r\nContent-Length: " << message.length() << "\r\n\r\n" << message;
           }
           catch(exception& e) {
               *response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n" << e.what();
           }
       };

    server.resource["^/mysql/([0-9]+)$"]["PUT"]=[](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
              try {
                  ptree pt;
                  read_json(request->content, pt);
                  string number=request->path_match[1];
                  mysql_update_data(pt,stoi(number));
                  string message = "Data updated";

                  *response << "HTTP/1.1 200 OK\r\nContent-Length: " << message.length() << "\r\n\r\n" << message;
              }
              catch(exception& e) {
                  *response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n" << e.what();
              }
          };

    //Default GET-example. If no other matches, this anonymous function will be called.
    //Will respond with content in the web/-directory, and its subdirectories.
    //Default file: index.html
    //Can for instance be used to retrieve an HTML 5 client that uses REST-resources on this server
    server.default_resource["GET"]=[&server](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
        const auto web_root_path=boost::filesystem::canonical("web");
        boost::filesystem::path path=web_root_path;
        path/=request->path;
        if(boost::filesystem::exists(path)) {
            path=boost::filesystem::canonical(path);
            //Check if path is within web_root_path
            if(distance(web_root_path.begin(), web_root_path.end())<=distance(path.begin(), path.end()) &&
               equal(web_root_path.begin(), web_root_path.end(), path.begin())) {
                if(boost::filesystem::is_directory(path))
                    path/="index.html";
                if(boost::filesystem::exists(path) && boost::filesystem::is_regular_file(path)) {
                    auto ifs=make_shared<ifstream>();
                    ifs->open(path.string(), ifstream::in | ios::binary);

                    if(*ifs) {
                        //read and send 128 KB at a time
                        streamsize buffer_size=131072;
                        auto buffer=make_shared<vector<char> >(buffer_size);

                        ifs->seekg(0, ios::end);
                        auto length=ifs->tellg();

                        ifs->seekg(0, ios::beg);

                        *response << "HTTP/1.1 200 OK\r\nContent-Length: " << length << "\r\n\r\n";
                        default_resource_send(server, response, ifs, buffer);
                        return;
                    }
                }
            }
        }
        string content="Could not open path "+request->path;
        *response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << content.length() << "\r\n\r\n" << content;
    };

    thread server_thread([&server](){
        //Start server
        server.start();
    });

    server_thread.join();

    return 0;
}

void default_resource_send(const HttpServer &server, shared_ptr<HttpServer::Response> response,
                           shared_ptr<ifstream> ifs, shared_ptr<vector<char> > buffer) {
    streamsize read_length;
    if((read_length=ifs->read(&(*buffer)[0], buffer->size()).gcount())>0) {
        response->write(&(*buffer)[0], read_length);
        if(read_length==static_cast<streamsize>(buffer->size())) {
            server.send(response, [&server, response, ifs, buffer](const boost::system::error_code &ec) {
                if(!ec)
                    default_resource_send(server, response, ifs, buffer);
                else
                    cerr << "Connection interrupted" << endl;
            });
        }
    }
}


string mysql_getAll(){

	string message = "{";
	cout << "Let's have MySQL get all data from DB" << endl;

	try {
	  sql::Driver *driver;
	  sql::Connection *con;
	  sql::ResultSet *res;
	  sql::PreparedStatement *pstmt;


	  /* Create a connection */
	  driver = get_driver_instance();
	  con = driver->connect("tcp://127.0.0.1:3306", "root", "root");

	  /* Connect to the MySQL test database */
	  con->setSchema("test");
	   cout<< endl;

	  /* Select in ascending order */
	  pstmt = con->prepareStatement("SELECT * FROM student");
	  res = pstmt->executeQuery();
		  while (res->next()) {
			  message+="\r\n{";
			  message+="\"id\":";
			  message+=to_string(res->getInt("id"));
			  message+= " ,\r\n";
			  message+="\"firstname\":\"" + res->getString("firstname") + "\",\r\n";
			  message+="\"lastname\":\"" + res->getString("lastname") + "\",\r\n";
			  message+="\"dept\":\"" + res->getString("dept") + "\"\r\n},";
		  }

		  message =  message.substr(0, message.size()-1);
		  cout << message <<endl;
		  message.append("}");

	  delete pstmt;
	  delete con;

	} catch (sql::SQLException &e) {
	  cout << "# ERR: SQLException in " << __FILE__;
	  cout << "(" << __FUNCTION__ << ") on line " << __LINE__ << endl;
	  cout << "# ERR: " << e.what();
	  cout << " (MySQL error code: " << e.getErrorCode();
	  cout << ", SQLState: " << e.getSQLState() << " )" << endl;
	}
	catch(std::exception &e1){
		cout << e1.what();
	}

	cout << message;

	return message;
}
string mysql_get_data(int id){

	string message="";
	cout << "Let's have MySQL receive string  from student of id = " << id << endl;

	try {
	  sql::Driver *driver;
	  sql::Connection *con;
	  sql::ResultSet *res;
	  sql::PreparedStatement *pstmt;

	  /* Create a connection */
	  driver = get_driver_instance();
	  con = driver->connect("tcp://127.0.0.1:3306", "root", "root");

	  /* Connect to the MySQL test database */
	  con->setSchema("test");

	  /* Select in ascending order */
	  pstmt = con->prepareStatement("SELECT * FROM student WHERE id = ?");
	  pstmt->setInt(1, id);
	  res = pstmt->executeQuery();

	  while (res->next()) {
		  message+="{";
		  			  message+="\"id\":";
		  			  message+=to_string(res->getInt("id"));
		  			  message+= " ,\r\n";
		  			  message+="\"firstname\":\"" + res->getString("firstname") + "\",\r\n";
		  			  message+="\"lastname\":\"" + res->getString("lastname") + "\",\r\n";
		  			  message+="\"dept\":\"" + res->getString("dept") + "\"\r\n}";
	  }

	  delete res;
	  delete pstmt;
	  delete con;

	} catch (sql::SQLException &e) {
	  cout << "# ERR: SQLException in " << __FILE__;
	  cout << "(" << __FUNCTION__ << ") on line " << __LINE__ << endl;
	  cout << "# ERR: " << e.what();
	  cout << " (MySQL error code: " << e.getErrorCode();
	  cout << ", SQLState: " << e.getSQLState() << " )" << endl;
	}
	catch(std::exception &e1){
		cout << e1.what();
	}

	return message;
}

void mysql_set_data(ptree pt){

	cout << "Let's have MySQL write into database" << endl;

	string first_name = pt.get<string>("firstName");
	string last_name = pt.get<string>("lastName");
	string dept = pt.get<string>("dept");

	cout << "Request :: \n firstName = " << first_name << endl;
	cout << "lastName = " << last_name << endl;
	cout << "dept = " << dept << endl;
	try {
	  sql::Driver *driver;
	  sql::Connection *con;
	  sql::PreparedStatement *pstmt;

	  /* Create a connection */
	  driver = get_driver_instance();
	  con = driver->connect("tcp://127.0.0.1:3306", "root", "root");

	  /* Connect to the MySQL test database */
	  con->setSchema("test");
	   cout<< endl;

	  /* '?' is the supported placeholder syntax */
	  pstmt = con->prepareStatement("INSERT INTO student(id,firstname,lastname,dept) VALUES (0,?,?,?)");
	    pstmt->setString(1, first_name);
	    pstmt->setString(2, last_name);
	    pstmt->setString(3, dept);
	    pstmt->executeUpdate();

	  delete pstmt;
	  delete con;


	} catch (sql::SQLException &e) {
	  cout << "# ERR: SQLException in " << __FILE__;
	  cout << "(" << __FUNCTION__ << ") on line " << __LINE__ << endl;
	  cout << "# ERR: " << e.what();
	  cout << " (MySQL error code: " << e.getErrorCode();
	  cout << ", SQLState: " << e.getSQLState() << " )" << endl;
	}
	catch(std::exception &e1){
		cout << e1.what();
	}

	cout<< "I am at the end of mysql_set_data method";
}

void mysql_update_data(ptree pt, int id){

	cout << "Let's have MySQL update into database" << endl;

	string first_name = pt.get<string>("firstName");
	string last_name = pt.get<string>("lastName");
	string dept = pt.get<string>("dept");

	cout << "Request :: \n firstName = " << first_name << endl;
	cout << "lastName = " << last_name << endl;
	cout << "dept = " << dept << endl;
	try {
	  sql::Driver *driver;
	  sql::Connection *con;
	  sql::PreparedStatement *pstmt;

	  /* Create a connection */
	  driver = get_driver_instance();
	  con = driver->connect("tcp://127.0.0.1:3306", "root", "root");

	  /* Connect to the MySQL test database */
	  con->setSchema("test");
	   cout<< endl;

	  /* '?' is the supported placeholder syntax */
	  pstmt = con->prepareStatement("UPDATE student SET firstname=?, lastname =? , dept=? WHERE id = ?");
	    pstmt->setString(1, first_name);
	    pstmt->setString(2, last_name);
	    pstmt->setString(3, dept);
	    pstmt->setInt(4,id);
	    pstmt->executeUpdate();

	  delete pstmt;
	  delete con;


	} catch (sql::SQLException &e) {
	  cout << "# ERR: SQLException in " << __FILE__;
	  cout << "(" << __FUNCTION__ << ") on line " << __LINE__ << endl;
	  cout << "# ERR: " << e.what();
	  cout << " (MySQL error code: " << e.getErrorCode();
	  cout << ", SQLState: " << e.getSQLState() << " )" << endl;
	}
	catch(std::exception &e1){
		cout << e1.what();
	}

	cout<< "I am at the end of mysql_set_data method";
}
