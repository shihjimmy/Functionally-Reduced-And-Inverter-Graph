/****************************************************************************
  FileName     [ guiServer.cpp ]
  PackageName  [ main ]
  Synopsis     [ A tiny local web server: the browser front end of fraig ]
****************************************************************************/

/**************************************************************************
   The GUI is a second front end next to the command line. It does NOT
   call the circuit code directly: every button sends a command string,
   which goes through the same CmdParser as typed commands. So the CLI and
   the GUI can never behave differently.

   Endpoints (all on 127.0.0.1 only):
     GET  /            the page (guiPage.h)
     GET  /api/state   output of "CIRJson": circuit + flow stage
     POST /api/cmd     body = one command line; returns its printed
                       output and the new state
     GET  /api/files   .aag files under the current directory

   One request at a time, no threads: commands change one global circuit,
   so serving requests in order is the simplest correct design.
**************************************************************************/

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <thread>
#include "cmdParser.h"
#include "guiServer.h"
#include "guiPage.h"

using namespace std;


namespace {

// ---------------------------------------------------------------------------
// Background jobs
//
// A command such as CIRFraig or CIRVerify can run for minutes, so the HTTP
// loop never runs one itself: POST /api/cmd hands the command to a worker
// thread and returns at once; the page then polls GET /api/job.  While a job
// runs, the HTTP thread must not touch the circuit or cout; it only reads
// the job record below, under its mutex.
//
// Progress needs no special API: commands already print progress as text
// overwritten with '\r' (e.g. "Sweeping nodes 2048/133151").  Everything the
// command prints goes into Job::output, and the page shows the last line.
// ---------------------------------------------------------------------------
struct Job
{
   std::mutex    lock;
   bool          running = false;
   bool          hasResult = false;   // finished, result not yet collected
   string        cmd;
   string        output;              // grows while the command runs
   int           status = 0;
   string        state;               // CIRJson after the command
};

// A streambuf that appends to Job::output under the lock.
class JobOutput : public streambuf
{
public:
   explicit JobOutput(Job& j) : _job(j) {}
protected:
   int overflow(int c) override {
      if (c != EOF) { std::lock_guard g(_job.lock); _job.output += char(c); }
      return c;
   }
   streamsize xsputn(const char* s, streamsize n) override {
      std::lock_guard g(_job.lock);
      _job.output.append(s, size_t(n));
      return n;
   }
private:
   Job& _job;
};

// The server: one per runGuiServer() call.  It runs commands through the
// CmdParser it was given (stage 8: there is no global cmdMgr) and owns the
// background job's state.
class GuiServer
{
public:
   explicit GuiServer(CmdParser& parser) : _parser(parser) {}
   void handle(int fd);

private:
   string runCommand(const string& line, CmdExecStatus& status);
   string currentState();
   void   runJob(string cmd);
   string jobJson();

   CmdParser& _parser;   // owned by main(), outlives the server
   Job        _job;
};

// Run one command line and return everything it printed (stdout+stderr).
string
GuiServer::runCommand(const string& line, CmdExecStatus& status)
{
   ostringstream captured;
   streambuf* oldOut = cout.rdbuf(captured.rdbuf());
   streambuf* oldErr = cerr.rdbuf(captured.rdbuf());
   status = _parser.execCmdLine(line);
   cout.flush();
   cout.rdbuf(oldOut);
   cerr.rdbuf(oldErr);
   return captured.str();
}

string
jsonEscape(const string& s)
{
   string out;
   out.reserve(s.size() + 16);
   for (unsigned char c : s) {
      switch (c) {
         case '"':  out += "\\\""; break;
         case '\\': out += "\\\\"; break;
         case '\n': out += "\\n"; break;
         case '\r': out += "\\r"; break;
         case '\t': out += "\\t"; break;
         default:
            if (c < 0x20) out += ' ';
            else out += char(c);
      }
   }
   return out;
}

// The CIRJson command prints exactly one JSON object.
string
GuiServer::currentState()
{
   CmdExecStatus status;
   string out = runCommand("CIRJson", status);
   const size_t b = out.find('{'), e = out.rfind('}');
   if (b == string::npos || e == string::npos) return "null";
   return out.substr(b, e - b + 1);
}

// "q" would ask a yes/no question on the terminal; the GUI cannot answer.
bool
isQuitCommand(const string& line)
{
   istringstream is(line);
   string cmd;
   is >> cmd;
   for (char& c : cmd) c = char(toupper(c));
   return !cmd.empty() && string("QUIT").compare(0, cmd.size(), cmd) == 0;
}

// The .aag files under the current directory, at most 2 directories deep,
// skipping hidden ones.  std::filesystem (C++17) replaces opendir/readdir/
// stat; the error_code overloads never throw (an unreadable directory is
// just skipped).
std::vector<std::string>
findAagFiles()
{
   namespace fs = std::filesystem;
   std::vector<std::string> files;
   std::error_code ec;
   fs::recursive_directory_iterator it(".", fs::directory_options::skip_permission_denied, ec), end;
   for (; !ec && it != end; it.increment(ec)) {
      const fs::path& p = it->path();
      const bool isDir = it->is_directory(ec);
      if (p.filename().string()[0] == '.') {          // hidden: skip (and do not enter)
         if (isDir) it.disable_recursion_pending();
         continue;
      }
      if (isDir) {
         if (it.depth() >= 2) it.disable_recursion_pending();
      }
      else if (p.extension() == ".aag")
         files.push_back(p.lexically_relative(".").string());
   }
   std::sort(files.begin(), files.end());
   return files;
}

string
fileListJson()
{
   const std::vector<std::string> files = findAagFiles();
   string json = "[";
   for (size_t i = 0; i < files.size(); ++i)
      json += (i ? ",\"" : "\"") + jsonEscape(files[i]) + "\"";
   return json + "]";
}

void
sendAll(int fd, const string& data)
{
   size_t sent = 0;
   while (sent < data.size()) {
      ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
      if (n <= 0) return;
      sent += size_t(n);
   }
}

void
reply(int fd, int code, const string& type, const string& body)
{
   ostringstream os;
   const char* text = code == 200 ? " OK" : code == 202 ? " Accepted" : code == 400 ? " Bad Request"
                    : code == 409 ? " Conflict" : " Not Found";
   os << "HTTP/1.1 " << code << text << "\r\n"
      << "Content-Type: " << type << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Cache-Control: no-store\r\n"
      << "Connection: close\r\n\r\n" << body;
   sendAll(fd, os.str());
}

// Read one HTTP request: request line, headers, and a Content-Length body.
bool
readRequest(int fd, string& method, string& path, string& body)
{
   string data;
   char buf[8192];
   size_t headerEnd;
   while ((headerEnd = data.find("\r\n\r\n")) == string::npos) {
      ssize_t n = recv(fd, buf, sizeof(buf), 0);
      if (n <= 0 || data.size() > (1u << 20)) return false;
      data.append(buf, size_t(n));
   }
   istringstream head(data.substr(0, headerEnd));
   head >> method >> path;

   size_t length = 0;
   string line;
   while (getline(head, line)) {
      for (size_t i = 0; i < line.size() && line[i] != ':'; ++i)
         line[i] = char(tolower(line[i]));
      if (line.compare(0, 15, "content-length:") == 0)
         length = strtoul(line.c_str() + 15, 0, 10);
   }
   body = data.substr(headerEnd + 4);
   while (body.size() < length) {
      ssize_t n = recv(fd, buf, sizeof(buf), 0);
      if (n <= 0) return false;
      body.append(buf, size_t(n));
   }
   body.resize(length);
   return true;
}


// Worker thread body: run the command with cout/cerr going to Job::output.
void
GuiServer::runJob(string cmd)
{
   JobOutput buf(_job);
   streambuf* oldOut = cout.rdbuf(&buf);
   streambuf* oldErr = cerr.rdbuf(&buf);
   const CmdExecStatus status = _parser.execCmdLine(cmd);
   cout.flush();
   cout.rdbuf(oldOut);
   cerr.rdbuf(oldErr);
   const string state = currentState();
   std::lock_guard g(_job.lock);
   _job.status = int(status);
   _job.state = state;
   _job.running = false;
   _job.hasResult = true;
}

// What a terminal would show on the last line of "out": the text after the
// last '\r' that is not blank (progress is overwritten with '\r').
string
progressLine(const string& out)
{
   size_t end = out.size();
   if (end && out[end - 1] == '\n') --end;
   const size_t begin = out.rfind('\n', end ? end - 1 : 0);
   string line = out.substr(begin == string::npos ? 0 : begin + 1,
                            end - (begin == string::npos ? 0 : begin + 1));
   string best;
   size_t from = 0;
   while (from <= line.size()) {
      size_t cr = line.find('\r', from);
      if (cr == string::npos) cr = line.size();
      const string piece = line.substr(from, cr - from);
      if (piece.find_first_not_of(' ') != string::npos) best = piece;
      from = cr + 1;
   }
   return best;
}

// GET /api/job: {"running", "cmd", "progress"}; once the job is finished,
// also its "status", full "output" and the new "state" (returned only once).
string
GuiServer::jobJson()
{
   std::lock_guard g(_job.lock);
   string json = string("{\"running\":") + (_job.running ? "true" : "false") +
                 ",\"cmd\":\"" + jsonEscape(_job.cmd) + "\"" +
                 ",\"progress\":\"" + jsonEscape(progressLine(_job.output)) + "\"";
   if (_job.hasResult) {
      json += ",\"done\":true,\"status\":" + to_string(_job.status) +
              ",\"output\":\"" + jsonEscape(_job.output) + "\",\"state\":" + _job.state;
      _job.hasResult = false;
   }
   return json + "}";
}

void
GuiServer::handle(int fd)
{
   string method, path, body;
   if (!readRequest(fd, method, path, body)) return;

   if (method == "GET" && (path == "/" || path == "/index.html"))
      reply(fd, 200, "text/html; charset=utf-8", kGuiPage);
   else if (method == "GET" && path == "/api/job")
      reply(fd, 200, "application/json", jobJson());
   else if (method == "GET" && path == "/api/state") {
      bool running;
      { std::lock_guard g(_job.lock); running = _job.running; }
      if (running) reply(fd, 409, "application/json", "{\"busy\":true}");
      else reply(fd, 200, "application/json", currentState());
   }
   else if (method == "GET" && path == "/api/files")
      reply(fd, 200, "application/json", fileListJson());
   else if (method == "POST" && path == "/api/cmd") {
      // Starts a job; the page polls /api/job for progress and the result.
      std::lock_guard g(_job.lock);
      if (_job.running) {
         reply(fd, 409, "application/json", "{\"busy\":true}");
         return;
      }
      if (isQuitCommand(body)) {
         reply(fd, 400, "application/json",
               "{\"error\":\"The GUI cannot quit fraig; press Ctrl-C in the terminal.\"}");
         return;
      }
      cout << "fraig(gui)> " << body << endl;   // echo on the terminal, before
      _job.running = true;                       // the worker redirects cout
      _job.hasResult = false;
      _job.cmd = body;
      _job.output.clear();
      thread(&GuiServer::runJob, this, body).detach();
      reply(fd, 202, "application/json", "{\"started\":true}");
   }
   else
      reply(fd, 404, "text/plain", "not found\n");
}

}  // namespace

int
runGuiServer(unsigned short port, CmdParser& parser)
{
   signal(SIGPIPE, SIG_IGN);   // a closed browser tab must not kill us

   int server = socket(AF_INET, SOCK_STREAM, 0);
   if (server < 0) { perror("socket"); return 1; }
   int yes = 1;
   setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

   sockaddr_in addr;
   memset(&addr, 0, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_port = htons(port);
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // this machine only
   if (bind(server, (sockaddr*)&addr, sizeof(addr)) < 0 ||
       listen(server, 16) < 0) {
      cerr << "Error: cannot listen on port " << port << " ("
           << strerror(errno) << ")" << endl;
      close(server);
      return 1;
   }
   cout << "fraig GUI: open http://localhost:" << port
        << " in your browser (Ctrl-C to stop)" << endl;

   GuiServer gui(parser);
   while (true) {
      int client = accept(server, 0, 0);
      if (client < 0) continue;
      gui.handle(client);
      close(client);
   }
}
