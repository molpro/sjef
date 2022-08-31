#include "sjef-program.h"
#include <iostream>
#include <sjef.h>
using Project = sjef::Project;
static const auto program_name = std::string("Simple Job Execution Framework");
#include <boost/filesystem.hpp>
#include <cstdlib>
#include <map>
#include <string>
#include <tclap/CmdLine.h>
#include <vector>

///> @private
extern "C" int sjef_program(int argc, char* argv[]) {
  std::string default_suffix{""};
  std::string eraseCandidate;
  try {

    TCLAP::CmdLine cmd(program_name + "\nThis software is based on pugixml library (http://pugixml.org). pugixml is "
                                      "Copyright (C) 2006-2018 Arseny Kapoulkine.",
                       ' ', SJEF_VERSION, true);

    std::vector<std::string> allowedCommands;
    std::string description{"specifies the action to be taken, and should be one of"};
    allowedCommands.push_back("import");
    description +=
        "\nimport: Following arguments give one or more files which should be copied into the project bundle.";
    allowedCommands.push_back("export");
    description += "\nexport: Following arguments give one or more files which should be copied out of the project "
                   "bundle. Each file can be prefixed with an absolute or relative directory name, default '.', which "
                   "specifies where the file will be copied to";
    allowedCommands.push_back("new");
    description += "\nnew: Make a completely new project bundle; the following argument gives the destination";
    allowedCommands.push_back("copy");
    description += "\ncopy: Make a copy of the project bundle; the following argument gives the destination";
    allowedCommands.push_back("move");
    description += "\nmove:  Move the project bundle; the following argument gives the destination";
    allowedCommands.push_back("edit");
    description += "\nedit: Edit the input file using ${VISUAL} (default ${EDITOR}, default vi)";
    allowedCommands.push_back("browse");
    description += "\nbrowse: Browse the output file using ${PAGER} (default less)";
    allowedCommands.push_back("clean");
    description += "\nclean: Remove obsolete files, and, optionally old run directories. Normally all but the most "
                   "recent run directory will be removed, but this can be modified using --run-directories";
    allowedCommands.push_back("run");
    description += "\nrun: Launch a job. Following arguments can specify any options to be given to the command run on "
                   "the backend.";
    allowedCommands.push_back("wait");
    description += "\nwait: Wait for completion of the job launched by run";
    allowedCommands.push_back("status");
    description += "\nstatus: Report the status of the job launched by run";
    allowedCommands.push_back("property");
    description += "\nproperty: Report the value of a property stored in the project registry";
    allowedCommands.push_back("kill");
    description += "\nkill: Kill the job launched by run";
    allowedCommands.push_back("erase");
    description += "\nerase: Erase the project";
    allowedCommands.push_back("get");
    description += "\nget: Obtain the values of one or more parameters, whose names are given as additional arguments "
                   "at the end of the command line";
    allowedCommands.push_back("set");
    description +=
        "\nset: Set the values of one or more parameters, giving additional arguments at the end of the command line "
        "in the form key1=value1 key2=value2...; if any values are empty, the corresponding parameter is deleted";
    allowedCommands.push_back("interactive");
    description += "\ninteractive: Enter interactive mode";
    TCLAP::ValuesConstraint<std::string> allowedVals(allowedCommands);
    TCLAP::MultiSwitchArg verboseSwitch("v", "verbose", "show detail", cmd, 0);
    TCLAP::UnlabeledValueArg<std::string> commandArg("command", description, true, "The subcommand", &allowedVals);
    cmd.add(commandArg);
    std::string backend_description{"Specify the backend where jobs will be run. This should be the name field of one "
                                    "of the entries configured in"};
    backend_description += " /usr/local/etc/sjef/<suffix>/backends.xml";
    backend_description += " ~/.sjef/<suffix>/backends.xml";
    backend_description += ", where <suffix> is the file extension of the project bundle, or \"local\", meaning run "
                           "<suffix> on the local machine. Default: local";
    TCLAP::ValueArg<std::string> backendSwitch("b", "backend", backend_description, false, "", "string", cmd);
    TCLAP::ValueArg<std::string> suffixSwitch("s", "suffix",
                                              "Specify the filename extension (without the leading .) of the project. "
                                              "This forces sjef to work only with projects of type suffix",
                                              false, "", "string", cmd);
    TCLAP::ValueArg<std::string> suffixInpSwitch(
        "", "suffix-inp", "Specify the filename extension (without the leading .) for input files", false, "inp",
        "string", cmd);
    TCLAP::ValueArg<std::string> suffixOutSwitch(
        "", "suffix-out", "Specify the filename extension (without the leading .) for output files", false, "out",
        "string", cmd);
    TCLAP::ValueArg<std::string> suffixXmlSwitch(
        "", "suffix-xml", "Specify the filename extension (without the leading .) for marked-up output files", false,
        "xml", "string", cmd);
    TCLAP::SwitchArg forceArg("f", "force", "Allow operations that would result in overwriting an existing file",
                              false);
    cmd.add(forceArg);
    TCLAP::ValueArg<int> run_directories("r", "run-directories",
                                         "Specify the number of run directories to retain in copy or clean", false, 1,
                                         "integer", cmd);
    TCLAP::SwitchArg nomonitorArg("M", "no-monitor", "Do not attempt to monitor job status or other background activity",
                              false);
    cmd.add(nomonitorArg);
    TCLAP::SwitchArg nosyncArg("S", "no-sync", "Do not attempt to synchronise with remote backend",
                              false);
    cmd.add(nosyncArg);
    TCLAP::SwitchArg waitArg("w", "wait", "Wait for completion of a job launched by run", false);
    cmd.add(waitArg);
    TCLAP::ValueArg<std::string> repeatArg("", "repeat", "Just for debugging", false, "1", "integer", cmd);
    TCLAP::UnlabeledValueArg<std::string> projectArg(
        "project",
        "The file name of the project bundle. If it has no extension and the -s flag has not been used, the extension "
        ".sjef is appended. If -s has been used, and the extension is absent or different to that specified, the -s "
        "extension is appended.",
        true, "The project file", "project-file", cmd);

    TCLAP::MultiArg<std::string> backendParameterArg(
        "p", "parameter", "parameter in the form key=value for completing backend run command template", false,
        "key=value", cmd);

    TCLAP::UnlabeledMultiArg<std::string> extraArg("additional", "Additional subcommand-specific arguments", false,
                                                   "additional arguments", cmd);

    cmd.parse(argc, argv);

    std::string project = projectArg.getValue();
    std::string command = commandArg.getValue();
    std::vector<std::string> extras = extraArg.getValue();
    if (verboseSwitch.getValue() > 0) {
      std::cout << "sjef " << command << " " << project;
      for (const auto& extra : extras)
        std::cout << " " << extra << std::endl;
      std::cout << std::endl;
    }
    if (extras.size() > 1 and
        (command != "import" and command != "export" and command != "run" and command != "property"))
      throw TCLAP::CmdLineParseException("Too many arguments on command line");
    bool success = true;
    { // scope for Project proj

      Project proj(project, true, suffixSwitch.getValue(),
                   {{"inp", suffixInpSwitch.getValue()},
                    {"out", suffixOutSwitch.getValue()},
                    {"xml", suffixXmlSwitch.getValue()}},
                    not nomonitorArg.getValue(), not nosyncArg.getValue(), nullptr);

      auto allowedBackends = proj.backend_names();
      auto backend = backendSwitch.getValue();
      if (backend.empty())
        backend = proj.property_get("backend");
      if (backend.empty())
        backend = "local";
      proj.change_backend(backend);
      if (verboseSwitch.getValue() > 1 or
          std::find(allowedBackends.begin(), allowedBackends.end(), backend) == allowedBackends.end()) {
        std::cout << "Project location: " << proj.filename() << std::endl;
        std::cout << "Project backend: " << proj.property_get("backend") << std::endl;
        std::cout << "Defined backends: " << std::endl;
        for (const auto& n : allowedBackends)
          std::cout << n << std::endl;
      }
      if (std::find(allowedBackends.begin(), allowedBackends.end(), backend) == allowedBackends.end())
        throw std::runtime_error("Backend " + backend + " not defined or invalid");

      auto property_process = [&proj](std::vector<std::string>& extras) {
        for (const auto& keyval : extras) {
          auto pos = keyval.find_first_of("=");
          auto key = keyval;
          std::string val;
          if (pos != std::string::npos) {
            key = keyval.substr(0, pos);
            val = keyval.substr(pos + 1);
            if (val.empty())
              proj.property_delete(key);
            else
              proj.property_set(key, val);
          }
          std::cout << "Property " << key << " = " << proj.property_get(key) << std::endl;
        }
      };

      for (int repeat = 0; repeat < std::stoi(repeatArg.getValue()); ++repeat) {
        if (command == "import")
          success = proj.import_file(extras, forceArg.getValue());
        else if (command == "export")
          success = proj.export_file(extras, forceArg.getValue());
        else if (command == "new") {
          success = proj.import_file(extras, forceArg.getValue());
        } else if (command == "copy")
          proj.copy(extras.front(), forceArg.getValue(), false, false, run_directories.getValue());
        else if (command == "move")
          success = proj.move(extras.front(), forceArg.getValue());
        else if (command == "erase")
          eraseCandidate = proj.filename().string();
        else if (command == "wait") {
          proj.wait();
        } else if (command == "status") {
          std::cout << proj.status_message(verboseSwitch.getValue()) << std::endl;
        } else if (command == "kill")
          proj.kill();
        else if (command == "run") {
          for (const auto& kv : backendParameterArg) {
            auto pos = kv.find_first_of("=");
            if (pos == std::string::npos)
              throw std::runtime_error("--parameter value must be of the form key=value");
            proj.backend_parameter_set(backend, kv.substr(0, pos), kv.substr(pos + 1));
          }
          if ((success = proj.run(backend, verboseSwitch.getValue(), forceArg.getValue(), waitArg.getValue())))
            std::cout << "Job number: " << proj.property_get("jobnumber") << std::endl;
          else if (proj.run_needed() or forceArg.getValue())
            std::cerr << "Run failed to start, or job number could not be captured" << std::endl
                      << "Status: " << proj.status_message(verboseSwitch.getValue()) << std::endl;
          else
            std::cerr << "Run not needed, so not started" << std::endl;
        } else if (command == "edit")
          success = system(("eval ${VISUAL:-${EDITOR:-vi}} \\'" + proj.filename("inp").string() + "\\'").c_str());
        else if (command == "browse") {
            success =
                system(("eval ${PAGER:-${EDITOR:-less}} \\'" + proj.filename("out", "", 0).string() + "\\'").c_str());
        } else if (command == "clean") {
          proj.clean(true, false, false, run_directories.getValue());
        } else if (command == "property") {
          property_process(extras);
        } else if (command == "interactive") {
          std::cout << "Interactive mode for project " << proj.filename() << std::endl;
          std::string line;
          std::string prompt{"? "};
          for (std::cout << prompt; std::getline(std::cin, line) and line != "exit"; std::cout << prompt) {
            auto pos = line.find(" ");
            auto command = (pos != std::string::npos ? line.substr(0, pos) : line);
            auto arguments = (pos != std::string::npos ? line.substr(pos) : std::string{});
            while (arguments.front() == ' ')
              arguments.erase(0, 1);
            //        std::cout << command << std::endl;
            //        std::cout << arguments << std::endl;
            if (command == "?" or command == "help")
              std::cout << "Allowed commands: status, backend, run, kill, wait, property, clean, edit, browse"
                        << std::endl;
            else if (command == "status")
              std::cout << "Status: " << proj.status_message(verboseSwitch.getValue()) << std::endl;
            else if (command == "backend") {
              proj.change_backend(arguments);
              std::cout << "backend changed to " << proj.property_get("backend") << std::endl;
            } else if (command == "run") {
              if (proj.run(proj.property_get("backend"), verboseSwitch.getValue(), true, false))
                std::cout << "Job number: " << proj.property_get("jobnumber") << std::endl;
              else
                std::cout << "Job submission failed" << std::endl;
            } else if (command == "kill") {
              proj.kill();
            } else if (command == "wait") {
              proj.wait();
            } else if (command == "property") {
              auto argv = std::vector<std::string>{arguments};
              property_process(argv);
            } else if (command == "clean") {
              proj.clean(true, false, false);
            } else if (command == "edit") {
              if (system(("eval ${VISUAL:-${EDITOR:-vi}} \\'" + proj.filename("inp").string() + "\\'").c_str()) != 0)
                throw std::runtime_error("Editor failed");
            } else if (command == "browse") {
              if (!proj.property_get("backend").empty()) {
                if (system(("eval ${PAGER:-${EDITOR:-less}} \\'" + proj.filename("out", "", 0).string() + "\\'")
                               .c_str()) != 0)
                  throw std::runtime_error("Editor failed");
              }
            } else
              std::cout << "Unknown command: " << line << std::endl;
          }
        } else
          throw TCLAP::CmdLineParseException("Unknown subcommand: " + command);
      }
    }
    if (not eraseCandidate.empty())
      sjef::Project::erase(eraseCandidate);
    return success ? 0 : 1;

  } catch (TCLAP::ArgException& e) // catch any exceptions
  {
    std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
  }
  return 0;
}