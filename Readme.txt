### Overview ###
The project implements a store which provides bids from different vendors to a requesting client.
The design which focusses on efficiency is built on a 'VendorClient' class which allows to asynchronously fetch bids from different vendors and a threadpool implementation to serve multiple client requests in parallel.

### Thread Pool ###
Worker threads created on initializing a 'threadpool' object waits on condition variables dedicated to each thread.
The 'threadpool' class provides a member function which identifies a free worker thread and executes the requested task in it. The delegated task is passed to the worker thread in form of a function pointer and function pointer argument.

### Serving Client Requests ###
main() thread in the store waits for client requests. The function processing a client is then assigned to the threadpool allowing to serve multiple client requests in parallel.

### Fetching Bids from Vendors ###
Asynchronous requests are sent to all the vendors before waiting for responses from each vendor. This is a faster approach compared to proceeding to next vendor only after receiving response from the previous vendor.

### References ###
Ideas/methods from the following sources were used for the implementation:
https://github.com/grpc/grpc/tree/master/examples/cpp/helloworld
https://www.ibm.com/docs/en/i/7.4?topic=ssw_ibm_i_74/apis/users_78.htm
https://developers.google.com/protocol-buffers/docs/cpptutorial