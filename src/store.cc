#include "threadpool.h"
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include "store.grpc.pb.h"
#include "vendor.grpc.pb.h"

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using grpc::Status;
using store::Store;
using store::ProductQuery;
using store::ProductReply;
using store::ProductInfo;
using vendor::Vendor;
using vendor::BidQuery;
using vendor::BidReply;

/* Implementation adapted from https://github.com/grpc/grpc/tree/master/examples/cpp/helloworld */

/* Function to be called from thread pool */
void delegated_function(void * arg);

class myStore final {
public:
  ~myStore()
  {
    server_->Shutdown();
    // Always shutdown the completion queue after the server.
    cq_->Shutdown();
  }

  // There is no shutdown handling in this code.
  void Run(std::string &vendorfile, std::string &server_address, threadpool &threadpoolmanager)
  {
    filename = vendorfile;
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);
    cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();
    HandleRpcs(filename, threadpoolmanager);
  }

  void delegated_work(void * arg)
  {
    class CallData *CallDataPtr = (class CallData *)arg;
    CallDataPtr->Proceed();
  }

private:
  class VendorClient {
  public:
    explicit VendorClient(std::shared_ptr<Channel> channel) : stub_(Vendor::NewStub(channel)) {}
    // Assembles the client's payload, sends it
    void getProductBid(const std::string& product_name)
    {
      vendorRequest.set_product_name(product_name);
      // stub_->PrepareAsyncgetProductBid() creates an RPC object, returning
      // an instance to store in "call" but does not actually start the RPC
      // Because we are using the asynchronous API, we need to hold on to
      // the "call" instance in order to get updates on the ongoing RPC.
      std::unique_ptr<ClientAsyncResponseReader<BidReply>> rpc(stub_->PrepareAsyncgetProductBid(&context, vendorRequest, &cq));
      // StartCall initiates the RPC call
      rpc->StartCall();
      // Request that, upon completion of the RPC, "reply" be updated with the
      // server's response; "status" with the indication of whether the operation
      // was successful. Tag the request with the integer 1.
      rpc->Finish(&vendorReply, &status, (void*)1);
    }

    // Presents the response back from the server.
    int waitProductBid(ProductReply *reply)
    {
      void* got_tag;
      bool ok = false;
      int ret_val = 1;

      // Block until the next result is available in the completion queue "cq".
      // The return value of Next should always be checked. This return value
      // tells us whether there is any kind of event or the cq_ is shutting down.
      GPR_ASSERT(cq.Next(&got_tag, &ok));
      // Verify that the result from "cq" corresponds, by its tag, our previous request.
      GPR_ASSERT(got_tag == (void*)1);
      // ... and that the request was completed successfully. Note that "ok"
      // corresponds solely to the request for updates introduced by Finish().
      GPR_ASSERT(ok);
      // Act upon the status of the actual RPC.
      if (status.ok())
      {
        ProductInfo *resp = reply->add_products();
        resp->set_price(vendorReply.price());
        resp->set_vendor_id(vendorReply.vendor_id());
        ret_val = 0;
      }

      return ret_val;
    }

  private:
    // Out of the passed in Channel comes the stub, stored here, our view of the server's exposed services.
    std::unique_ptr<Vendor::Stub> stub_;
    // Data we are sending to the server.
    BidQuery vendorRequest;
    // Container for the data we expect from the server.
    BidReply vendorReply;
    // Context for the client. It could be used to convey extra information to the server and/or tweak certain RPC behaviors.
    ClientContext context;
    // The producer-consumer queue we use to communicate asynchronously with the gRPC runtime.
    CompletionQueue cq;
    // Storage for the status of the RPC upon completion.
    Status status;
  };

  // Class encompasing the state and logic needed to serve a request.
  class CallData {
  public:
    // Take in the "service" instance (in this case representing an asynchronous
    // server) and the completion queue "cq" used for asynchronous communication
    // with the gRPC runtime.
    CallData(Store::AsyncService* service, ServerCompletionQueue* cq, std::string &filename)
      : service_(service), cq_(cq), responder_(&ctx_), status_(CREATE), vendorList(filename)
    {
      // Invoke the serving logic right away.
      Proceed();
    }

    void Proceed()
    {
      if (status_ == CREATE)
      {
        status_ = PROCESS;
        service_->RequestgetProducts(&ctx_, &request_, &responder_, cq_, cq_,this);
      }
      else if (status_ == PROCESS)
      {
        new CallData(service_, cq_, vendorList);
        std::vector<std::string> ip_addrresses;
        getVendorIpList(vendorList,ip_addrresses);
        numberOfVendors = ip_addrresses.size();
        std::string vendorInput;
        ProductInfo *vendorResponse;
        for(int i = 0; i < numberOfVendors; i++)
        {
		      VendorClient *vendorObj = new VendorClient(grpc::CreateChannel(ip_addrresses[i], grpc::InsecureChannelCredentials()));
          ClientComVec.push_back(vendorObj);
        }
        for(int i = 0; i < numberOfVendors; i++)
        {
          vendorInput = request_.product_name();
          (*ClientComVec[i]).getProductBid(vendorInput);
        }
        for(int i = 0; i < numberOfVendors; i++)
        {
          if((*ClientComVec[i]).waitProductBid(&reply_))
          {
            std::cerr << "error in waiting for product bid" << std::endl;
          }
        }
        status_ = FINISH;
        responder_.Finish(reply_, Status::OK, this);
      }
      else
      {
        GPR_ASSERT(status_ == FINISH);
        for(int i = 0; i < numberOfVendors; i++)
        {
          delete(ClientComVec[i]);
        }
        // Once in the FINISH state, deallocate ourselves (CallData).
        delete this;
      }
    }

    int getVendorIpList(std::string &filename, std::vector<std::string>&ip_addrresses)
    {
      std::ifstream myfile (filename);
      if (myfile.is_open())
      {
        std::string ip_addr;
        while (getline(myfile, ip_addr))
        { 
          ip_addrresses.push_back(ip_addr);
        }
        myfile.close();
      }
      else
      {
        std::cerr << "Failed to open file " << filename << std::endl;
        return EXIT_FAILURE;
      }

      return EXIT_SUCCESS;
    }

    bool inProcessState(void)
    {
      bool ret_val = false;
      if (status_ == PROCESS)
      {
        ret_val = true;
      }

      return ret_val;
    }

  private:
    // The means of communication with the gRPC runtime for an asynchronous server.
    Store::AsyncService* service_;
    // The producer-consumer queue where for asynchronous server notifications.
    ServerCompletionQueue* cq_;
    // Context for the rpc, allowing to tweak aspects of it such as the use
    // of compression, authentication, as well as to send metadata back to the
    // client.
    ServerContext ctx_;
    // What we get from the client.
    ProductQuery request_;
    // What we send back to the client.
    ProductReply reply_;
    // The means to get back to the client.
    ServerAsyncResponseWriter<ProductReply> responder_;
    // Let's implement a tiny state machine with the following states.
    enum CallStatus { CREATE, PROCESS, FINISH };
    CallStatus status_;  // The current serving state.
    std::string vendorList;
    int numberOfVendors;
    std::vector<VendorClient *> ClientComVec;
  };

  void HandleRpcs(std::string &filename, threadpool &threadpoolmanager)
  {
    new CallData(&service_, cq_.get(),filename);
    void* tag;  
    bool ok;
    bool status;

    while (true)
    {
      GPR_ASSERT(cq_->Next(&tag, &ok));
      GPR_ASSERT(ok);
      if (static_cast<CallData*>(tag)->inProcessState() == true)
      {
        status = false;
        while (status == false)
        {
          status = threadpoolmanager.work(delegated_function, (void *)static_cast<CallData*>(tag));
        }
      }
      else
      {
        static_cast<CallData*>(tag)->Proceed();
      }
    }
  }
  
  std::unique_ptr<ServerCompletionQueue> cq_;
  Store::AsyncService service_;
  std::unique_ptr<Server> server_;
  std::string filename;
};

myStore server;

void delegated_function(void * arg)
{
  server.delegated_work(arg);
}

int main(int argc, char** argv) {

  int addr_index = -1;
  std::string filename;
  std::string portaddr;
  int threadcount;

  if (argc == 4)
  {  
    filename = std::string(argv[1]);
    portaddr = std::string(argv[2]);
    threadcount = atoi(argv[3]);
  }
  else
  {
    std::cerr << "Exiting, correct usage: ./store <filepath for vendor addresses> <ip address:port to listen on for clients> <maximum number of threads in threadpool>" << std::endl;
    return EXIT_FAILURE;
  }
  threadpool threadpoolmanager(threadcount);
	server.Run(filename, portaddr, threadpoolmanager);

	return EXIT_SUCCESS;
}
