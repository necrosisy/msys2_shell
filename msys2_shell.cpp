#include <windows.h>
#include <filesystem>
#include <unordered_map>

namespace fs = std::filesystem;

static HANDLE h_job = nullptr;
static HANDLE h_process = nullptr;
static HANDLE h_thread = nullptr;

static std::string MSYSTEM{};

static std::unordered_map<std::wstring, std::string const> const NAME_TO_MSYSTEM{
		{L"msys.exe",       "MSYS\t"},
		{L"msys2.exe",      "MSYS\t"},
		{L"mingw32.exe",    "MINGW32\t"},
		{L"mingw64.exe",    "MINGW64\t"},
		{L"ucrt64.exe",     "UCRT64\t"},
		{L"clang64.exe",    "CLANG64\t"},
		{L"clang32.exe",    "CLANG32\t"},
		{L"clangarm64.exe", "CLANGARM64\t"},
};

static fs::path os_executable() {
	unsigned int bs = 128;
	std::wstring link_path(bs, wchar_t(0));
	while (true) {
		auto r = GetModuleFileNameW(
			nullptr,
			link_path.data(),
			bs
		);
		if (r < bs) {
			break;
		}
		bs += bs;
		link_path.resize(bs, wchar_t(0));
	}
	return link_path;
}

static void set_termination_job() {
	h_job = CreateJobObjectW(nullptr, nullptr);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_info{};
	job_info.BasicLimitInformation.LimitFlags =
		JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
	SetInformationJobObject(
		h_job,
		JobObjectExtendedLimitInformation,
		&job_info,
		sizeof(job_info)
	);
}

static fs::path resolve_exe_path() {
	auto lp = os_executable();

	auto it = NAME_TO_MSYSTEM.find(lp.filename());
	if (it == NAME_TO_MSYSTEM.end()) {
		exit(EXIT_FAILURE);
	}
	MSYSTEM = it->second;

	std::error_code ec{};
	auto target = fs::read_symlink(lp, ec);
	if (ec) {
		exit(EXIT_FAILURE);
	}

	target.replace_filename("sh.exe");
	lp = fs::read_symlink(target, ec);
	if (ec) {
		exit(EXIT_FAILURE);
	}

	return lp;
}

static void run_process() {
	auto path = resolve_exe_path();
	std::wstring cmd(L"\"" + path.wstring() + L"\" -l");

	std::string env{};
	env.append("PATH=\t");
	env.append("MSYS=winsymlinks:nativestrict\t");
	env.append("MSYSTEM=").append(MSYSTEM);
	env.append("CHERE_INVOKING=enabled_from_arguments\t");

	char* c_env = env.data();

	for (char* c = c_env; *c != '\0'; c++) {
		if (*c == '\t') {
			*c = '\0';
		}
	}

	PROCESS_INFORMATION proc_info{};
	STARTUPINFOW start_info{};
	GetStartupInfoW(&start_info);
	CreateProcessW(
		path.c_str(),
		cmd.data(),
		nullptr,
		nullptr,
		true,
		CREATE_SUSPENDED,
		(void*)c_env,
		nullptr,
		&start_info,
		&proc_info
	);
	h_process = proc_info.hProcess;
	h_thread = proc_info.hThread;
}

int main() {

	set_termination_job();
	run_process();

	ResumeThread(h_thread);
	SetConsoleCtrlHandler(nullptr, TRUE);
	AssignProcessToJobObject(h_job, h_process);
	WaitForSingleObject(h_process, INFINITE);

	h_process ? CloseHandle(h_process) : 0;
	h_job ? CloseHandle(h_job) : 0;
	h_thread ? CloseHandle(h_thread) : 0;

	return 0;
}