#distutils: language = c++
from libcpp.string cimport string
from libcpp.vector cimport vector
from libcpp.map cimport map
from libcpp cimport bool

cdef extern from "sjef/sjef.h" namespace "sjef":
    cdef enum status:
        unknown = 0
        running = 1
        waiting = 2
        completed = 3
        unevaluated = 4
        killed = 5
        failed = 6

    cdef cppclass Project:
        # constructor function
        Project(string, bool, string, map[string, string]) except +
        # Project(string, bool, string) except +
        string filename_string(string, string, int) except +
        bool copy(string &, bool, bool) except +
        bool move(string &, bool) except +
        void erase(string &) except +
        bool import_file(string, bool) except +
        bool export_file(string, bool) except +
        void clean(int) except +
        string xml() except +
        string file_contents(string, string) except +
        string file_contents(string) except +
        string input_from_output() except +
        bool run(int, bool, bool) except +
        bool run(string, int, bool, bool) except +
        bool run_needed(int) except +
        void kill() except +
        void wait() except +
        void wait(unsigned int) except +
        status status() except +
        string name() except +
        string filename_string() except +
        void property_set(map[string, string]) except +
        void property_delete(vector[string] &) except +
        map[string, string] property_get(vector[string] &) except +
        vector[string] property_names() except +
        size_t project_hash() except +
        size_t input_hash() except +
        int recent_find(string &) except +
        @staticmethod
        string recent(string&, int) except +
        map[string, string] backend_parameters(string &, bool) except +
        void backend_parameter_set(string &, string &, string &) except +
        void backend_parameter_delete(string &, string &) except +
        string backend_parameter_get(string &, string &) except +
        string backend_parameter_documentation(string &, string &) except +
        string backend_parameter_default(string &, string &) except +
        vector[string] xpath_search(string &, string &, int) except +

#    cdef string recent(string&, int) except +

# cdef extern from "molpro-project.h" namespace "molpro::project":
