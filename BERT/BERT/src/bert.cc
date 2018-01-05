// BERT.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "variable.pb.h"
#include "XLCALL.h"
#include "function_descriptor.h"
#include "bert.h"
#include "basic_functions.h"
#include "type_conversions.h"
#include "string_utilities.h"
#include "windows_api_functions.h"
#include "message_utilities.h"
#include "..\resource.h"

#include "excel_com_type_libraries.h"

#include <google\protobuf\util\json_util.h>

/** debug/util function */
void DumpJSON(const google::protobuf::Message &message, const char *path = 0) {
#ifdef _DEBUG
    std::string str;
    google::protobuf::util::JsonOptions opts;
    opts.add_whitespace = true;
    google::protobuf::util::MessageToJsonString(message, &str, opts);
    if (path) {
        FILE *f;
        fopen_s(&f, path, "w");
        if (f) {
            fwrite(str.c_str(), sizeof(char), str.length(), f);
            fflush(f);
        }
        fclose(f);
    }
    else DebugOut("%s\n", str.c_str());
#endif
}

BERT* BERT::instance_ = 0;

BERT* BERT::Instance() {
    if (!instance_) instance_ = new BERT;
    return instance_;
}

BERT::BERT()
    : child_process_id_(0)
    , buffer_(0)
    , dev_flags_(0)
    , stream_pointer_(0)
    , transaction_id_(0)
    , connected_(false) {

    APIFunctions::GetRegistryDWORD(dev_flags_, "BERT2.DevOptions");
}

void BERT::OpenConsole() {

    if (console_process_id_) {
        // ... show ...
    }
    else {
        StartConsoleProcess();
    }

}

void BERT::SetPointers(ULONG_PTR excel_pointer, ULONG_PTR ribbon_pointer) {

    application_dispatch_ = reinterpret_cast<LPDISPATCH>(excel_pointer);

    // marshall pointer
    AtlMarshalPtrInProc(application_dispatch_, IID_IDispatch, &stream_pointer_);

    BERTBuffers::CallResponse call;
    BERTBuffers::CallResponse response;

    int32_t key = object_map_.MapCOMPointer(reinterpret_cast<ULONG_PTR>(application_dispatch_));

    auto function_call = call.mutable_function_call();
    function_call->set_function("BERT$install.application.pointer");
    function_call->add_arguments()->set_num(key);
    
    auto function_descriptor = function_call->add_arguments();
    object_map_.DispatchToVariable(function_descriptor, application_dispatch_, true);

    RCall(response, call);

}

int BERT::StartConsoleProcess() {

    std::string console_command;
    std::string console_arguments;

    APIFunctions::GetRegistryString(console_arguments, "BERT2.ConsoleArguments");
    APIFunctions::GetRegistryString(console_command, "BERT2.ConsoleCommand");

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    ZeroMemory(&pi, sizeof(pi));

    int rslt = 0;
    char *args = new char[1024];

    // really? A: yes, it needs a non-const buffer
    sprintf_s(args, 1024, "\"%s\" %s -p %s -d %d", console_command.c_str(), console_arguments.c_str(), pipe_name_.c_str(), dev_flags_);

    if (!CreateProcessA(0, args, 0, 0, FALSE, 0, 0, 0, &si, &pi )){
        DebugOut("CreateProcess failed (%d).\n", GetLastError());
        rslt = GetLastError();
    }
    else {
        console_process_id_ = pi.dwProcessId;
        if (job_handle_) {
            if (!AssignProcessToJobObject(job_handle_, pi.hProcess))
            {
                DebugOut("Could not AssignProcessToObject\n");
            }
        }
    }

    delete[] args;

    return rslt;
}

int BERT::StartChildProcess() {

	std::stringstream path;

	int pathlen = ::GetEnvironmentVariableA("PATH", 0, 0);
	char *buffer = new char[pathlen + 1];
	if (pathlen > 0) ::GetEnvironmentVariableA("PATH", buffer, pathlen);
	buffer[pathlen] = 0;

	path << r_home_ << "\\";

#ifdef _WIN64
	path << "bin\\x64;";
#else
	path << "bin\\i386;";
#endif

	path << buffer;
	delete[] buffer;

	::SetEnvironmentVariableA("PATH", path.str().c_str());

	char *args = new char[1024];
	sprintf_s(args, 1024, "\"%s\" -p %s -r %s", child_path_.c_str(), pipe_name_.c_str(), r_home_.c_str());
	
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);

	ZeroMemory(&pi, sizeof(pi));

	int rslt = 0;

    DWORD creation_flags = CREATE_NO_WINDOW;

    if (dev_flags_) creation_flags = 0;

	if (!CreateProcessA(0, args, 0, 0, FALSE, creation_flags, 0, 0, &si, &pi))
	{
		DebugOut("CreateProcess failed (%d).\n", GetLastError());
		rslt = GetLastError();
	}
	else {
		child_process_id_ = pi.dwProcessId;
        if (job_handle_) {
            if (!AssignProcessToJobObject(job_handle_, pi.hProcess))
            {
                DebugOut("Could not AssignProcessToObject\n");
            }
        }
	}

	delete[] args;
	return rslt;
}

/**
 * call is not const because we're going to add a 
 * transaction ID for accounting purposes.
 */
BERTBuffers::CallResponse* BERT::RCall(BERTBuffers::CallResponse &response, BERTBuffers::CallResponse &call){

	uint32_t id = transaction_id_++;
	DWORD bytes;
	//std::string pb;

	call.set_id(id);
	//call.SerializeToString(&pb);

    std::string pb = MessageUtilities::Frame(call);

	ResetEvent(io_.hEvent);
	WriteFile(pipe_handle_, pb.c_str(), (int32_t)pb.length(), NULL, &io_);

	if (call.wait()) {

        ResetEvent(callback_info_.default_unsignaled_event_);
        HANDLE handles[2] = { io_.hEvent, callback_info_.default_unsignaled_event_ };

		ResetEvent(io_.hEvent);
		ReadFile(pipe_handle_, buffer_, 8192, 0, &io_);

        while (true) {
            ResetEvent(callback_info_.default_signaled_event_); // set unsignaled
            DWORD signaled = WaitForMultipleObjectsEx(2, handles, FALSE, INFINITE, FALSE);
            if (signaled == WAIT_OBJECT_0) {
                DWORD rslt = GetOverlappedResultEx(pipe_handle_, &io_, &bytes, INFINITE, FALSE);
                if (rslt) {
                    //if (!rsp.ParseFromArray(buffer_, bytes)) {
                    if (!MessageUtilities::Unframe(response, buffer_, bytes)) {
                        DebugOut("parse err!\n");
                        response.set_err("parse error (0x10)");
                    }
                }
                else {
                    DWORD err = GetLastError();
                    std::stringstream ss;
                    ss << "pipe error " << err;
                    response.set_err(ss.str());
                }
                break;
            }
            else {
                ResetEvent(callback_info_.default_unsignaled_event_);
                DebugOut("other handle signaled, do something\n");
                HandleCallbackOnThread();
                SetEvent(callback_info_.default_signaled_event_); // signal callback thread
            }
        }

	}
    SetEvent(callback_info_.default_signaled_event_); // default signaled

	return &response;
}

void BERT::HandleCallback() {

    // this function gets called from a thread (i.e. not the main excel thread),
    // so we cannot call the excel API but we can call excel via COM using the 
    // marshalled pointer.

    // if we want to support the Excel API (and I suppose we do) we'll need to 
    // get back on the main thread. we could do that with COM, and a call-through,
    // but I'm not sure if that will preserve context (from a function call).

    // if COM doesn't work in this situation we'll need to use an event we can 
    // signal the blocked thread, but we will need to know if we are in fact 
    // blocking that thread. we'll also need a way to pass data back and forth.

    // SO: COM doesn't work in that case. we need to signal. 

    // there are two possible cases. either we are being called from a spreadsheet
    // function or we're being called from a shell function. the semantics are different
    // because the main thread may or may not be blocked.

    DWORD wait_result = WaitForSingleObject(callback_info_.default_signaled_event_, 0);
    if (wait_result == WAIT_OBJECT_0) {
        // DebugOut("event 2 is already signaled; this is a shell function\n");

        BERTBuffers::CallResponse &call = callback_info_.callback_call_;
        BERTBuffers::CallResponse &response = callback_info_.callback_response_;

        if (stream_pointer_) {
            LPDISPATCH dispatch_pointer = 0;
            CComVariant variant_result;
            HRESULT hresult = AtlUnmarshalPtr(stream_pointer_, IID_IDispatch, (LPUNKNOWN*)&dispatch_pointer);

            if (SUCCEEDED(hresult)) {
                CComQIPtr<Excel::_Application> application(dispatch_pointer);
                if (application) {
                    CComVariant variant_macro = "BERT.ContextSwitch";
                    CComVariant variant_result = application->Run(variant_macro);
                }
                else response.set_err("qi failed");
                dispatch_pointer->Release();
            }
            else response.set_err("unmarshal failed");
        }
        else {
            response.set_err("invalid stream pointer");
        }

    }
    else {
        DebugOut("event 2 is not signaled; this is a spreadsheet function\n");
        DebugOut("callback waiting for signal\n");

        // let main thread handle
        SetEvent(callback_info_.default_unsignaled_event_); 
        WaitForSingleObject(callback_info_.default_signaled_event_, INFINITE);
        DebugOut("callback signaled\n");
    }
}

int BERT::HandleCallbackOnThread() {

    BERTBuffers::CallResponse &call = callback_info_.callback_call_;
    BERTBuffers::CallResponse &response = callback_info_.callback_response_;

    int return_value = 0;

    // DumpJSON(call);
    response.set_id(call.id());

    if (call.operation_case() == BERTBuffers::CallResponse::OperationCase::kFunctionCall) {

        auto callback = call.function_call();
        auto function = callback.function();

        if (!function.compare("excel")) {
            return_value = ExcelCallback();
        }
        else if (!function.compare("release-pointer")) {
            if (callback.arguments_size() > 0) {
                int32_t key = callback.arguments(0).num();
                DebugOut("release pointer 0x%x\n", key);
                object_map_.RemoveCOMPointer(key);
            }
        }
        else {
            response.mutable_result()->set_boolean(false);
        }
    }
    else if (call.operation_case() == BERTBuffers::CallResponse::OperationCase::kComCallback) {
        object_map_.InvokeCOMFunction(call.com_callback(), response);
    }
    else {
        response.mutable_result()->set_boolean(false);
    }

    // response.mutable_value()->set_boolean(true);
    return return_value;

}

int BERT::ExcelCallback() {

    BERTBuffers::CallResponse &call = callback_info_.callback_call_;
    BERTBuffers::CallResponse &response = callback_info_.callback_response_;
    
    auto callback = call.function_call();
    auto function = callback.function();

    int32_t command = 0;
    int32_t success = -1;

    if (callback.arguments_size() > 0) {
        auto arguments_array = callback.arguments(0).arr();

        int count = arguments_array.data().size();
        if (count > 0) {
            command = (int32_t)arguments_array.data(0).num();
        }
        if (command) {
            XLOPER12 excel_result;
            std::vector<LPXLOPER12> excel_arguments;
            for (int i = 1; i < count; i++) {
                LPXLOPER12 argument = new XLOPER12;
                excel_arguments.push_back(Convert::VariableToXLOPER(argument, arguments_array.data(i)));
            }
            if (excel_arguments.size()) success = Excel12v(command, &excel_result, (int32_t)excel_arguments.size(), &(excel_arguments[0]));
            else success = Excel12(command, &excel_result, 0, 0);
            Convert::XLOPERToVariable(response.mutable_result(), &excel_result);
            Excel12(xlFree, 0, 1, &excel_result);
        }
    }

    return success;
}

void BERT::RunCallbackThread() {

    int buffer_size = 1024 * 8;
    char *buffer = new char[buffer_size];
    std::stringstream ss;
    ss << "\\\\.\\pipe\\" << pipe_name_ << "-CB";

    HANDLE callback_pipe_handle = CreateFileA(ss.str().c_str(), GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, 0);
    if (!callback_pipe_handle || callback_pipe_handle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        DebugOut("ERR opening pipe: %d\n", err);
    }
    else {

        DebugOut("Connected to callback pipe\n");

        DWORD mode = PIPE_READMODE_MESSAGE;
        BOOL state = SetNamedPipeHandleState(callback_pipe_handle, &mode, 0, 0);

        DWORD bytes = 0;
        OVERLAPPED io;
        //bool reading = true;

        if (stream_pointer_) {
            DebugOut("stream pointer already set\n");
        }

        memset(&io, 0, sizeof(io));
        io.hEvent = CreateEvent(0, TRUE, FALSE, 0);
        ReadFile(callback_pipe_handle, buffer, buffer_size, 0, &io);
        while (true) {
            DWORD result = WaitForSingleObject(io.hEvent, 1000);
            if (result == WAIT_OBJECT_0) {
                ResetEvent(io.hEvent);
                DWORD rslt = GetOverlappedResultEx(callback_pipe_handle, &io, &bytes, 0, FALSE);
                if (rslt) {
                    
                    BERTBuffers::CallResponse &call = callback_info_.callback_call_;
                    BERTBuffers::CallResponse &response = callback_info_.callback_response_;

                    call.Clear();
                    response.Clear();
                    MessageUtilities::Unframe(call, buffer, bytes);

                    HandleCallback();
                    //DumpJSON(response);

                    if (call.wait()) {
                        std::string str_response = MessageUtilities::Frame(response);
                        //memcpy(buffer, str_response.c_str(), str_response.length());
                        DebugOut("callback writing response (%d)\n", str_response.size());

                        // block?
                        //WriteFile(callback_pipe_handle, buffer, (int32_t)str_response.size(), &bytes, &io);
                        WriteFile(callback_pipe_handle, str_response.c_str(), (int32_t)str_response.size(), &bytes, &io);
                        result = GetOverlappedResultEx(callback_pipe_handle, &io, &bytes, INFINITE, FALSE);
                        DebugOut("result %d; wrote %d bytes\n", result, bytes);
                    }

                    // restart
                    ResetEvent(io.hEvent);
                    ReadFile(callback_pipe_handle, buffer, buffer_size, 0, &io);

                }
                else {
                    DWORD err = GetLastError();
                    DebugOut("ERR in GORE: %d\n", err);
                    // ...
                    break;
                }
            }
            else if (result != WAIT_TIMEOUT) {
                DebugOut("callback pipe error: %d\n", GetLastError());
                break;
            }
        }

        CloseHandle(io.hEvent);
        DisconnectNamedPipe(callback_pipe_handle);
        CloseHandle(callback_pipe_handle);
    }
    delete buffer;
}

unsigned __stdcall BERT::CallbackThreadFunction(void *param) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    // FIXME: why not just use instance here?

    BERT *bert = reinterpret_cast<BERT*>(param);
    bert->RunCallbackThread();
    CoUninitialize();
    return 0;
}

void BERT::Init(){

    APIFunctions::GetRegistryString(r_home_, "BERT2.RHome");
    APIFunctions::GetRegistryString(child_path_, "BERT2.ControlRCommand");

    // the job object created here is used to kill child processes
    // in the event of an excel exit (for any reason).

    job_handle_ = CreateJobObject(0, 0);
    if (job_handle_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
        memset(&jeli, 0, sizeof(jeli));
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_handle_, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli)))
        {
            DebugOut("Could not SetInformationJobObject\n");
        }
    }
    else {
        DebugOut("Create job object failed\n");
    }

    // create pipe name from excel pid, we just need it to be session-unique.
    // pipe name can be overridden in the registry for dev

    APIFunctions::GetRegistryString(pipe_name_, "BERT2.OverridePipeName");
    if (!pipe_name_.length()) {
        std::stringstream ss;
        ss << "BERT2-PIPE-" << _getpid();
        pipe_name_ = ss.str();
    }

    int rslt = StartChildProcess();
	int errs = 0;	
	
	buffer_ = new char[8192];
	io_.hEvent = CreateEvent(0, TRUE, TRUE, 0); // FIXME: clean this up

	if (!rslt) {

        std::string full_name = "\\\\.\\pipe\\";
        full_name.append(pipe_name_);

		while (1) {
			pipe_handle_ = CreateFileA(full_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, 0);
			if (!pipe_handle_ || pipe_handle_ == INVALID_HANDLE_VALUE) {
				DWORD err = GetLastError();
				DebugOut("ERR opening pipe: %d\n", err);
				if (errs++ > 10) break;
				Sleep(100);
			}
			else {

				DWORD mode = PIPE_READMODE_MESSAGE;
				BOOL state = SetNamedPipeHandleState(pipe_handle_, &mode, 0, 0);

				connected_ = true;
				DebugOut("Connected (errs: %d)\n", errs);

                uintptr_t callback_thread_ptr = _beginthreadex(0, 0, BERT::CallbackThreadFunction, this, 0, 0);

                std::string library_path;
                APIFunctions::GetRegistryString(library_path, "BERT2.LibraryPath");
                library_path = StringUtilities::EscapeBackslashes(library_path);

                std::string library_command = "library(BERTModule, lib.loc = \"";
                library_command += library_path;
                library_command += "\")";

                // get embedded startup code, split into lines
                // FIXME: why do we require that this be in multiple lines?

                std::string startup_code = APIFunctions::ReadResource(MAKEINTRESOURCE(IDR_RCDATA1));
                std::vector<std::string> lines;
                StringUtilities::Split(startup_code, '\n', 1, lines, true);

				BERTBuffers::CallResponse call;

                // should maybe wait on this, so we know it's complete before we do next steps?
                // A: no, the R process will queue it anyway (implicitly), all this does is avoid wire traffic

				call.set_wait(false);
                auto code = call.mutable_code();
                code->add_line(library_command);
                for( auto line:lines) code->add_line(line);

				BERTBuffers::CallResponse rsp;
				RCall(rsp, call);

				break;
			}
		}
	}

    if(dev_flags_) StartConsoleProcess();

}

void BERT::MapFunctions() {
	
	if (!connected_) return;

	BERTBuffers::CallResponse call;
	BERTBuffers::CallResponse rsp;

	call.mutable_function_call()->set_function("BERT$list.functions");
	call.set_wait(true);

	RCall(rsp, call);

	function_list_.clear();
	if (rsp.operation_case() == BERTBuffers::CallResponse::OperationCase::kErr) return; // error: no functions
	int count = 0;

	// shoehorning functions into our very simple variable syntax results 
	// in a lot of nesting. it's not clear that order is guaranteed here,
	// although that has more to do with R than protobuf.

	// this is a mess. should create a dedicated message for this (...)

	auto ParseArguments = [](BERTBuffers::Variable &args) {
		ARGUMENT_LIST arglist;
		for (auto arg : args.arr().data()) {
			std::string name;
			std::string defaultValue;
			for (auto field : arg.arr().data()) {
				if (!field.name().compare("name")) name = field.str();
				if (!field.name().compare("default")) {
					std::stringstream ss;
					switch (field.value_case()) {
					case BERTBuffers::Variable::ValueCase::kBoolean:
						ss << field.boolean() ? "TRUE" : "FALSE";
						break;
					case BERTBuffers::Variable::ValueCase::kNum:
						ss << field.num();
						break;
					case BERTBuffers::Variable::ValueCase::kStr:
						ss << '"' << field.str() << '"';
						break;
					}
					defaultValue = ss.str();
				}
			}
			if (name.length()) {
				arglist.push_back(std::make_shared<ArgumentDescriptor>(name, defaultValue));
			}
		}
		return arglist;
	};

	for( auto descriptor: rsp.result().arr().data()){
		std::string function;
        ARGUMENT_LIST arglist;
		for (auto entry : descriptor.arr().data()) {
			if (!entry.name().compare("name")) function = entry.str();
			else if (!entry.name().compare("arguments")) arglist = ParseArguments(entry);
		}
		if (function.length()) {
			function_list_.push_back(std::make_shared<FunctionDescriptor>(function, "", "", arglist));
		}
	}

}

void BERT::Close() {

    // free marshalled pointer
    if (stream_pointer_) AtlFreeMarshalStream(stream_pointer_);

	if (connected_) {

		BERTBuffers::CallResponse call;
		BERTBuffers::CallResponse rsp;

		call.set_wait(false);
		call.set_control_message("shutdown");
		RCall(rsp, call);

		connected_ = false;
		CloseHandle(pipe_handle_);
		pipe_handle_ = 0;
	}

    // child processes

	if (buffer_) delete buffer_;
}
