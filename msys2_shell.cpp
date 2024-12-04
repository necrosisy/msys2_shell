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

static std::wstring os_executable() {
	unsigned int buf_size = 128;
	std::wstring symlink_path(buf_size, wchar_t(0));
	while (true) {
		auto length = GetModuleFileNameW(
			nullptr,
			symlink_path.data(),
			buf_size
		);
		if (length < buf_size) {
			break;
		}
		buf_size += buf_size;
		symlink_path.resize(buf_size);
	}
	return symlink_path;
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

static std::wstring resolve_symlink(fs::path const& symlink_path) {

	auto it = NAME_TO_MSYSTEM.find(symlink_path.filename().c_str());
	if (it == NAME_TO_MSYSTEM.end()) {
		exit(EXIT_FAILURE);
	}
	MSYSTEM = it->second;

	std::error_code ec{};
	auto target_path = fs::read_symlink(symlink_path, ec);
	if (ec) {
		exit(EXIT_FAILURE);
	}
	target_path.replace_filename(L"sh.exe");
	auto exe_path = fs::read_symlink(target_path, ec);
	if (ec) {
		exit(EXIT_FAILURE);
	}
	if (!fs::is_regular_file(exe_path, ec)) {
		exit(EXIT_FAILURE);
	}
	return exe_path;
}


static void run_process() {

	std::wstring path{ resolve_symlink(os_executable()) };

	std::wstring cmd{};
	cmd.append(L"\"").append(path).append(L"\" -l");

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
		path.data(),
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