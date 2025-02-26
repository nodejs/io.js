# C embedding API

This file is an overview for C embedding API.
It is mostly to catch all the work in progress.

## The API overview

### Error handling API
- `node_embedding_on_error`

### Global platform API
- `node_embedding_set_api_version`
- `node_embedding_run_main`
- `node_embedding_create_platform`
- `node_embedding_delete_platform`
- `node_embedding_platform_set_flags`
- `node_embedding_platform_get_parsed_args`

### Runtime API
- `node_embedding_run_runtime`
- `node_embedding_create_runtime`
- `node_embedding_delete_runtime`
- `node_embedding_runtime_set_flags`
- `node_embedding_runtime_set_args`
- `node_embedding_runtime_on_preload`
- `node_embedding_runtime_on_start_execution`
- `node_embedding_runtime_add_module`
- [ ] add API to handle unhandled exceptions

### Runtime API to run event loops
- `node_embedding_on_wake_up_event_loop`
- `node_embedding_run_event_loop`
- `node_embedding_complete_event_loop`
- [ ] add API for emitting `beforeExit` event
- [ ] add API for emitting `exit` event
- [ ] add API to stop the event loop

### Runtime API to interop with Node-API
- `node_embedding_run_node_api`
- `node_embedding_open_node_api_scope`
- `node_embedding_close_node_api_scope`

## Functional overview

### Platform API

- Global handling of C API errors
- [ ] Global handling of Node.js/V8 errors (is it possible?)
- [ ] Global handling of unhandled JS errors

- API version
- Node-API version

- Global platform initialization
- Global platform uninitialization
- Parsing the command line parameters
- Controlled by the platform flags
- Get parsed command line arguments

- [ ] Can we initialize platform again if it returns early?
- [-] Will not support: custom thread pool

- [ ] API v-table to avoid DLL named function binding

### Runtime API

- Runtime initialization
- Runtime uninitialization
- Set runtime args
- Set runtime flags
- Register a preload callback
- Register start execution callback
- [ ] Load default Node.js snapshot without the custom start execution callback
- [ ] Get the returned value from the start execution
- Register linked modules

- [ ] Runtime handling of API errors (is it possible?)
- [ ] Runtime handling of Node.js/V8 errors (is it possible?)
- [ ] Runtime handling of unhandled JS errors

- [ ] Events on Runtime destruction (beforeExit, exit)
- [ ] Main vs secondary Runtimes
- [ ] Worker thread runtimes (is it possible?)
- [ ] Associate Inspector with the Runtime
- [ ] Inspector for the secondary Runtimes
- [ ] Runtime destructor to clean up all related resources including the
      pending tasks.
- [ ] Exit process only for unhandled main runtime errors.
- [ ] Have an internal list of all runtimes in the system.
      It is a list of all secondary runtimes attached to the main runtime.

### Event Loop API

- Run event loop while it has events (default)
- Run event loop once and block for an event
- Run event loop once and no wait
- Run event loop till completion
- [ ] Interrupt the event loop (uv_stop stops the default loop and then
      the loop resets it)
- [ ] Loop while some condition is true
- [ ] Custom foreground task runner (is it possible?)
- [ ] Notify if event loop has work to do (based on Electron PollEvents)
      It must be blocked while the loop is running
      It is only for the main runtime
- [ ] V8 Microtask posting and draining
- [ ] Complete the loop immediately if needed
- [ ] Protect from nested uv_run calls
- [ ] How to post setImmediate from another thread?

### Node-API integration

- Run Node-API code as a lambda
- Explicitly open and close the Node-API scope
- [ ] Handle JS errors

### Test TODOs

- [ ] Test passing the V8 thread pool size.
- [ ] Add tests based on the environment and platform `cctest`s.
- [ ] Enable the test_main_modules_node_api test.
- [ ] Test failure in Preload callback.
- [ ] Test failure in linked modules.
- [ ] Add a test that handles JS errors.
- [ ] Make sure that the the delete calls match the create calls.
