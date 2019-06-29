#include <iostream>

#include <webserver/http_server.hpp>
#include "mock_api.h"

int main(){
	try{
		HttpServer server;
		server.config.port = 8000;
		
		MockApi api;
		
		/* TODO set here callbacks */
		
		server.resource["^/time$"]["GET"] = [=](std::shared_ptr<Responce> responce,
												std::shared_ptr<Request> request){
			
			/* TODO bind this function */
			std::thread work_thread([responce]){
				
			});
			work_thread.detach();
			if(work_thread.joinable()){
				/* TODO print error*/
				//std::throw std::runtime_error;
			}
		}
		
		server.start();
	}
	catch(const std::exception& ex){
		std::cerr<< ex.what() << std::endl;
	}
	
	return 0;
}