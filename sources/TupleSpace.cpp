#include "TupleSpace.h"
namespace Linda{
    namespace{
        Tuple find (const Pattern& pattern, const std::string & file_path, bool remove, std::chrono::milliseconds timeout){
            //todo implement removing while browsing
            //todo implement timeout
            int fd = open(file_path.c_str(), O_CREAT | O_RDWR, 0666);
            if(fd < 0){
                std::string errno_msg = strerror(errno);
                throw Exception::TupleSpaceException("Could not open tuple file. " + errno_msg);
            }

            char read_buffer[ENTRY_SIZE];
            memset(&read_buffer, 0, sizeof(read_buffer));
            //todo set lock
            while(::read(fd, read_buffer, sizeof (ENTRY_SIZE)) > 0){
                //todo release lock
                if(read_buffer[0] == Linda::BUSY_FLAG){
                    std::vector<ISerializable::serialization_type> tuple_vec;
                    unsigned long i = 1;
                    auto c = static_cast<unsigned char> (read_buffer[i]);
                    while(c!=Tuple::SerializationCodes::END && i < ENTRY_SIZE-1){
                        c = static_cast<unsigned char>(read_buffer[i]);
                        tuple_vec.push_back(c);
                        i++;
                    }
                    Tuple t;
                    t.deserialize(tuple_vec);
                    if(pattern.check(t)){
                        close(fd);
                        return t;
                    }
                }
            }
            return Tuple();
        }

        void enqueue(Pattern pattern, const std::string & file_path, const char& type){
            pid_t pid = getpid();
            auto serialized_pattern = pattern.serialize();
            unsigned char buffer[MAX_TUPLE_SIZE + LIST_HEADER_SIZE + 1];

            buffer[0] = type;
            memcpy(buffer + 1, &pid, sizeof (pid));
            for(unsigned long i = 0; i < MAX_TUPLE_SIZE - LIST_HEADER_SIZE; i++){
                buffer[i+ LIST_HEADER_SIZE] = serialized_pattern[i];
            }
            buffer[LIST_HEADER_SIZE + MAX_TUPLE_SIZE] = '\n'; //for readability

            int queue_fd = open(file_path.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0666);
            if(queue_fd < 0){
                std::string errno_msg = strerror(errno);
                throw Exception::TupleSpaceException("Error when opening queue file. " + errno_msg);
            }

            //lock whole file
            struct flock lock{};
            memset(&lock, 0, sizeof (lock));
            lock.l_type = F_WRLCK | F_RDLCK;
            lock.l_whence = SEEK_SET;

            if(fcntl(queue_fd, F_SETLKW, &lock) == -1){
                close(queue_fd);
                std::string errno_msg = strerror(errno);
                throw Exception::TupleSpaceException("Could not acquire queue file lock. " + errno_msg);
            }

            if(write(queue_fd, buffer, MAX_TUPLE_SIZE + LIST_HEADER_SIZE + 1) < 0){
                lock.l_type = F_UNLCK;
                if(fcntl(queue_fd, F_SETLKW, &lock) == -1){
                    close(queue_fd);
                    std::string errno_msg = strerror(errno);
                    throw Exception::TupleSpaceException("Error releasing file lock. " + errno_msg);
                }
                std::string errno_msg = strerror(errno);
                throw Exception::TupleSpaceException("Error writing process to queue. " + errno_msg);
            }
            //release lock after writing
            lock.l_type = F_UNLCK;
            if(fcntl(queue_fd, F_SETLKW, &lock) == -1){
                close(queue_fd);
                throw Linda::Exception::TupleSpaceException("Error releasing file lock");
            }
            close(queue_fd);
        }

        void dequeue(const std::vector<std::string> & queue_paths){
            //todo implement
        }
    }
    void create(bool no_exist_err, const std::string& path, const std::string& name) {
        const std::filesystem::path tuplespace_path{path + "/" + name};

        if(std::filesystem::exists(tuplespace_path) ){
            if(no_exist_err){
                return;
            }
            throw Exception::TupleSpaceCreateException("\"" + name + "\" already exists in " + path);
        }else{
            try{
                std::filesystem::create_directories(tuplespace_path);
            }catch (std::filesystem::filesystem_error & ex){
                std::string msg = "Filesystem error. ";
                msg.append(ex.what());
                throw Exception::TupleSpaceCreateException(msg);
            }
        }
        std::ofstream file(tuplespace_path.string() + "/.tuplespace");
        file.close();
    }

    void connect(const std::string& path) {
        if(std::filesystem::exists(path)){
            if(std::filesystem::exists(path + "/.tuplespace")){
                State & state = State::getInstance();
                state.tupleSpacePath = path;
                state.connected = true;
            }else{
                throw Exception::TupleSpaceConnectException(" \"" + path + "\" not a tuplespace.");
            }
        }else{
            throw Exception::TupleSpaceConnectException(" \"" + path + "\" does not exist.");
        }
    }

    void output(Tuple tuple) {
        if(!State::getInstance().connected){
            throw Exception::TupleSpaceConnectException("Not connected to any tuplespace.");
        }
        State& state = State::getInstance();

        //find file
        //fixme can this cause process to hang?
        std::string filePath = state.tupleSpacePath + "/" + tuple.path() + ".linda";
        int flags = O_CREAT | O_RDWR;
        int fd = open(filePath.c_str(), flags, 0666);
        if(fd < 0){
            std::cerr<<strerror(errno)<<std::endl;
            throw Exception::TupleSpaceException("Could not open tuple file");
        }

        //prep read buffer
        unsigned char read_buffer[Linda::ENTRY_SIZE];
        memset(&read_buffer, 0, sizeof (read_buffer));

        //lock file
        struct flock lock;
        memset(&lock, 0, sizeof (lock));
        lock.l_type = F_WRLCK | F_RDLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0; //lock whole file

        if(fcntl(fd, F_SETLKW, &lock) == -1){
            close(fd);
            std::cerr<<strerror(errno)<<std::endl;
            throw Exception::TupleSpaceException("Could not acquire file lock");
        }

        //todo add max file size check
        ssize_t bytes_read;
        bytes_read = ::read(fd, read_buffer, Linda::ENTRY_SIZE);
        while(bytes_read == Linda::ENTRY_SIZE){
            if((read_buffer[0] & EMPTY_FLAG )== EMPTY_FLAG){
                break;
            }
            bytes_read = ::read(fd, read_buffer, Linda::ENTRY_SIZE);
        }

        auto serialized = tuple.serialize();
        //todo guard against too long tuples
        unsigned char entry[Linda::ENTRY_SIZE];
        memset(entry, int('-'), sizeof(entry));
        entry[0] = Linda::BUSY_FLAG;
        for (unsigned long i = 0; i< serialized.size(); i++){
            entry[i+1] = serialized[i];
        }
        entry[Linda::ENTRY_SIZE -1] = '\n';

        if(write(fd, entry, Linda::ENTRY_SIZE) == -1){
            lock.l_type = F_UNLCK;
            if(fcntl(fd, F_SETLKW, &lock) == -1){
                close(fd);
                std::cerr<<strerror(errno)<<std::endl;
                throw Exception::TupleSpaceException("Error releasing file lock");
            }
            std::cerr<<strerror(errno)<<std::endl;
            throw Exception::TupleSpaceException("Couldn't write tuple to file");
        }

        //unlock
        lock.l_type = F_UNLCK;
        if(fcntl(fd, F_SETLKW, &lock) == -1){
            close(fd);
            throw Exception::TupleSpaceException("Error releasing file lock");
        }
        close(fd);


        //todo find waiting processes and send signals
    }

    //to jest usuwające
    Tuple input(Pattern pattern, std::chrono::milliseconds timeout) {
        std::chrono::time_point<std::chrono::system_clock> begin_t = std::chrono::system_clock::now();
        if(!State::getInstance().connected){
            throw Exception::TupleSpaceConnectException("Not connected to any tuplespace.");
        }
        State& state = State::getInstance();

        std::vector<std::string> enqeued_in;
        try{
            const std::vector<std::string > & node_paths = pattern.all_paths();
            for(auto & path : node_paths){
                //todo recalculate timeout with each iteration -> probably needed for file locks
                std::string node_path = state.tupleSpacePath + "/" + path + ".linda";
                Tuple t = find(pattern, node_path, true, timeout);
                if(t.size() > 0){
                    //tuple found, remove from kłełełes and return
                    dequeue(enqeued_in);
                    return t;
                }
                std::string queue_path = state.tupleSpacePath + "/" + path + "-queue.linda";;
                enqueue(pattern, queue_path, Linda::INPUT_FLAG);
                enqeued_in.push_back(queue_path);
            }
        }catch (const Exception::TupleSpaceException& ex){
            //todo remove process from kłełełes and re-throw exception?
            dequeue(enqeued_in);
            throw ex;   //todo nie wiem czy to na pewno tak jak się powinno to robić
        }
        //todo sleep waiting for a signal
        return Tuple();



        return Tuple();
    }

    //to jest nieusuwające
    Tuple read(Pattern pattern, std::chrono::milliseconds timeout) {
        std::chrono::time_point<std::chrono::system_clock> begin_t = std::chrono::system_clock::now();
        if(!State::getInstance().connected){
            throw Exception::TupleSpaceConnectException("Not connected to any tuplespace.");
        }
        State& state = State::getInstance();

        std::vector<std::string> enqeued_in;
        try{
            const std::vector<std::string > & node_paths = pattern.all_paths();
            for(auto & path : node_paths){
                //todo recalculate timeout with each iteration -> probably needed for file locks
                std::string node_path = state.tupleSpacePath + "/" + path + ".linda";
                Tuple t = find(pattern, node_path, false, timeout);
                if(t.size() > 0){
                    //tuple found, remove from kłełełes and return
                    dequeue(enqeued_in);
                    return t;
                }
                std::string queue_path = state.tupleSpacePath + "/" + path + "-queue.linda";;
                enqueue(pattern, queue_path, Linda::READ_FLAG);
                enqeued_in.push_back(queue_path);
            }
        }catch (const Exception::TupleSpaceException& ex){
            //todo remove process from kłełełes and re-throw exception?
            dequeue(enqeued_in);
            throw ex;   //todo nie wiem czy to na pewno tak jak się powinno to robić
        }
        //todo sleep waiting for a signal
        return Tuple();
    }
}