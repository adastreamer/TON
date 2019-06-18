#pragma once

#include <memory>
#include <string>

#include <webserver/Responce.hpp>


class MockApi{
public:
	MockApi();
	~MockApi() = default;
	
	MockApi(const MockApi&)=delete;
	MockApi& operator=(const MockApi&)=delete;
	MockApi(const MockApi&&)=delete;
	
	
	void mockGetFoo();
private:
	void web_error_response(std::shared_ptr<SimpleWeb::Response> response, std::string msg);
	void web_success_response(std::shared_ptr<SimpleWeb::Response> response, std::string msg);
};