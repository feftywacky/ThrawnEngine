#include <stdio.h>
#include <sys/time.h>

#include <stdlib.h>
#include <vector>
#include <cstring>
#include <string>
#include <chrono>
#include <sstream>
#include <unistd.h>

#include <iostream>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <cstdint>
    #include <pthread.h>
    #include <fcntl.h>
    #include <termios.h>
    #include <sys/ioctl.h>

    // Define placeholders for Windows types on non-Windows systems
    typedef pthread_t HANDLE;
    typedef uint32_t DWORD;
    typedef int BOOL;
    typedef void* LPVOID;
    typedef uint32_t* LPDWORD;

    const HANDLE STD_INPUT_HANDLE = 0;

    #define TRUE 1
    #define FALSE 0

    #define ENABLE_MOUSE_INPUT 0x0010
    #define ENABLE_WINDOW_INPUT 0x0008

    HANDLE GetStdHandle(DWORD) {
        return fileno(stdin); // On POSIX systems, fileno returns the file descriptor for stdin
    }

    BOOL GetConsoleMode(HANDLE hConsole, DWORD* lpMode) {
        // This function is a stub and always returns FALSE
        // You can add platform-specific code if needed
        if (isatty(hConsole)) {
            *lpMode = 0; // Just a placeholder value
            return TRUE;
        }
        return FALSE;
    }

    BOOL SetConsoleMode(HANDLE hConsole, DWORD dwMode) {
        // This function is a stub and always returns TRUE
        // You can add platform-specific code if needed
        return TRUE;
    }

    void FlushConsoleInputBuffer(HANDLE hConsole) {
        // This function is a stub // ???????????????????????????????????????????????
        tcflush(hConsole, TCIFLUSH); // Clear the input buffer for POSIX systems
    }

    BOOL PeekNamedPipe(HANDLE hNamedPipe, LPVOID lpBuffer, DWORD nBufferSize, LPDWORD lpBytesRead, LPDWORD lpTotalBytesAvail, LPDWORD lpBytesLeftThisMessage) {
        // This function is a stub // ???????????????????????????????????????????????
        // On POSIX systems, use ioctl to get the number of bytes available
        int bytesAvailable;
        if (ioctl(hNamedPipe, FIONREAD, &bytesAvailable) != -1) {
            if (lpTotalBytesAvail) {
                *lpTotalBytesAvail = bytesAvailable;
            }
            return TRUE;
        }
        return FALSE;
    }

    BOOL GetNumberOfConsoleInputEvents(HANDLE hConsoleInput, LPDWORD lpcNumberOfEvents) {
        // This function is a stub // ???????????????????????????????????????????????
        // On POSIX systems, use ioctl to get the number of bytes available
        int bytesAvailable;
        if (ioctl(hConsoleInput, FIONREAD, &bytesAvailable) != -1) {
            *lpcNumberOfEvents = bytesAvailable;
            return TRUE;
        }
        return FALSE;
    }

#endif



#include "uci.h"
#include "move_generator.h"
#include "move_helpers.h"
#include "bitboard_helpers.h"
#include "transposition_table.h"
#include "bitboard.h"
#include "fen.h"
#include "search.h"
#include "misc.h"

using namespace std;

/*
TIMING CONTROL AND UCI
UCI PROTOCOL CODE REFERENCES TO VICE CHESS ENGINE BY RICHARD ALBERT
*/

// exit from engine flag
int quit = 0;

// UCI "movestogo" command moves counter
int movestogo = 30;

// UCI "movetime" command time counter
int movetime = -1;

// UCI "time" command holder (ms)
int uci_time = -1;

// UCI "inc" command's time increment holder
int inc = 0;

// UCI "starttime" command time holder
int starttime = 0;

// UCI "stoptime" command time holder
int stoptime = 0;

// variable to flag time control availability
int timeset = 0;

// variable to flag when the time is up
int stopped = 0;


/*
TIME CONTROL
*/
int get_time_ms() {
    return chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now().time_since_epoch()).count();
}

int input_waiting() 
{
    static int init = 0, pipe;
    static HANDLE inh;
    DWORD dw;

    if (!init)
    {
        init = 1;

        #ifdef _WIN32
            inh = GetStdHandle(STD_INPUT_HANDLE);
        #else
            // Non-Windows equivalent code to get the standard input handle
            inh = fileno(stdin); // On POSIX systems, fileno returns the file descriptor for stdin
        #endif

        pipe = !GetConsoleMode(inh, &dw);
        if (!pipe)
        {
            SetConsoleMode(inh, dw & ~(ENABLE_MOUSE_INPUT|ENABLE_WINDOW_INPUT));
            FlushConsoleInputBuffer(inh);
        }
    }
    
    if (pipe)
    {
        if (!PeekNamedPipe(inh, NULL, 0, NULL, &dw, NULL)) return 1;
        return dw;
    }
    
    else
    {
        GetNumberOfConsoleInputEvents(inh, &dw);
        return dw <= 1 ? 0 : dw;
    }
    
}

void read_input() {
    // Bytes to read holder
    int bytes;

    // GUI/user input
    char input[256] = "", *endc;

    // "Listen" to STDIN
    if (input_waiting()) {
        // cout<<"input waiting"<<"\n";
        stopped = 1;

        // Loop to read bytes from STDIN
        do {
            bytes = read(fileno(stdin), input, 256);
        }

        // Until bytes are available
        while (bytes < 0);

        // Searches for the first occurrence of '\n'
        endc = strchr(input, '\n');

        // If a newline character is found, set its value at the pointer to 0
        if (endc)
            *endc = 0;

        // If input is available
        if (strlen(input) > 0) {
            // Match UCI "quit" command
            if (!strncmp(input, "quit", 4)) {
                // Tell the engine to terminate execution
                quit = 1;
            }

            // Match UCI "stop" command
            else if (!strncmp(input, "stop", 4)) {
                // Tell the engine to terminate execution
                quit = 1;
            }
        }
    }
}

// a bridge function to interact between search and GUI input
void communicate() {
	// if time is up break here
    if(timeset == 1 && get_time_ms() > stoptime) {
        // cout<<"communicate set stopped = 1"<<"\n";
		stopped = 1;
	}
	
    // read GUI input
	read_input();
}

/*
UCI PROTOCOL
*/

int uci_parse_move(const char *move_str)
{
    std::vector<int> moves = generate_moves();
    
    int source = (move_str[0] - 'a') + (8-(move_str[1]- '0')) * 8;
    int target = (move_str[2] - 'a') + (8-(move_str[3]- '0')) * 8;
    int promoted_piece = 0;

    for (int move : moves)
    {
        if (source == get_move_source(move) && target == get_move_target(move))
        {
            promoted_piece = get_promoted_piece(move);

            if (promoted_piece)
            {
                if ((promoted_piece == Q || promoted_piece == q) && move_str[4] == 'q')
                    return move;
                else if ((promoted_piece == R || promoted_piece == r) && move_str[4] == 'r')
                    return move;
                else if ((promoted_piece == N || promoted_piece == n) && move_str[4] == 'b')
                    return move;
                else if ((promoted_piece == B || promoted_piece == b) && move_str[4] == 'n')
                    return move;
                continue;
            }

            // legal move
            return move;
        }
    }

    // illegal move ie. puts king in check
    return 0;
}

void uci_parse_position(const char *command) {
    // Create a non-const pointer for manipulation
    cout<<command<<endl;
    const char *non_const_command = command;

    non_const_command += 9; // shift index to skip 'position' in command

    const char *curr_ch = non_const_command;

    // parse 'startpos' command
    
    if (strncmp(non_const_command, "startpos", 8) == 0)
        parse_fen(start_position);

    // parse 'fen' command
    else 
    {
        curr_ch = strstr(non_const_command, "fen");

        // no 'fen' command found
        if (curr_ch == nullptr)
            parse_fen(start_position);

        else {
            // shift index to next token
            curr_ch += 4;
            parse_fen(curr_ch);
        }
    }

    // parse moves after position is set up
    curr_ch = strstr(non_const_command, "moves");
    
    // if moves are found
    if (curr_ch != nullptr) {

        curr_ch += 6;

        while (*curr_ch) {
            int move = uci_parse_move(curr_ch);

            if (move == 0)
                break;

            repetition_index++;
            repetition_table[repetition_index] = zobristKey;

            make_move(move, all_moves);

            // Move index to the end of the current move
            while (*curr_ch && *curr_ch != ' ')
                curr_ch++;

            // go to next move
            curr_ch++;
        }

    }

    // for debug
    // print_board(colour_to_move);

}

void uci_parse_go(const char* command)
{
    reset_time_control();
    int depth = -1;

    // Infinite search
    if (strstr(command, "infinite") != nullptr) {}

    // Match UCI "binc" command
    if (strstr(command, "binc") != nullptr && colour_to_move == 1) {
        // Parse black time increment
        inc = atoi(strstr(command, "binc") + 5);
    }

    // Match UCI "winc" command
    if (strstr(command, "winc") != nullptr && colour_to_move == 0) {
        // Parse white time increment
        inc = atoi(strstr(command, "winc") + 5);
    }

    // Match UCI "wtime" command
    if (strstr(command, "wtime") != nullptr && colour_to_move == 0) {
        // Parse white time limit
        uci_time = atoi(strstr(command, "wtime") + 6);
    }

    // Match UCI "btime" command
    if (strstr(command, "btime") != nullptr && colour_to_move == 1) {
        // Parse black time limit
        uci_time = atoi(strstr(command, "btime") + 6);
    }

    // Match UCI "movestogo" command
    if (strstr(command, "movestogo") != nullptr) {
        // Parse number of moves to go
        movestogo = atoi(strstr(command, "movestogo") + 10);
    }

    // Match UCI "movetime" command
    if (strstr(command, "movetime") != nullptr) {
        // Parse amount of time allowed to spend to make a move
        movetime = atoi(strstr(command, "movetime") + 9);
    }

    // Match UCI "depth" command
    if (strstr(command, "depth") != nullptr) {
        // Parse search depth
        depth = atoi(strstr(command, "depth") + 6);
    }

    // If move time is available, set time and moves to go
    if (movetime != -1) {
        uci_time = movetime;
        movestogo = 1;
    }

    // Initialize start time
    starttime = get_time_ms();
    
    depth = depth;

    // If time control is available
    if (uci_time != -1) {
        // Set the timeset flag
        timeset = 1;

        // Set up timing
        uci_time /= movestogo;
        uci_time -= 50; // lag compensation 

        if (uci_time < 0)
        {
            uci_time = 0;
            inc -= 50;
            if (inc<0) 
                inc = 1;
        }
        stoptime = starttime + uci_time + inc;
    }

    // If depth is not available, set depth to 64 plies
    if (depth == -1) {
        depth = 64;
    }

    // Print debug info
    std::cout << "time:" << uci_time << " start:" << static_cast<unsigned int>(starttime) << " stop:" << static_cast<unsigned int>(stoptime)
              << " depth:" << depth << " timeset:" << timeset << std::endl;

    // Search position
    search_position(depth);
}

void uci_loop()
{
    // just make it big enough
    #define INPUT_BUFFER 20000
    
    int max_hashmap_size = 1024; // 1GB
    int mb = 128; // default 128 MB

    // reset STDIN & STDOUT buffers
    setbuf(stdin, NULL);
    setbuf(stdout, NULL);
    
    // define user / GUI input buffer
    char input[INPUT_BUFFER];

    while (true)
    {
        // reset user /GUI input
        memset(input, 0, sizeof(input));
        
        // make sure output reaches the GUI
        fflush(stdout);
        
        // get user / GUI input
        if (!fgets(input, INPUT_BUFFER, stdin))
            continue;
        
        // make sure input is available
        if (input[0] == '\n')
            continue;
        
        // parse UCI "isready" command
        if (strncmp(input, "isready", 7) == 0)
        {
            std::cout << "readyok\n";
            continue;
        }
        
        // parse UCI "position" command
        else if (strncmp(input, "position", 8) == 0)
        {
            uci_parse_position(input);
            reset_hashmap();
        }

        // parse UCI "ucinewgame" command
        else if (strncmp(input, "ucinewgame", 10) == 0)
        {
            uci_parse_position("position startpos");
            reset_hashmap();
        }
        // parse UCI "go" command
        else if (strncmp(input, "go", 2) == 0)
            uci_parse_go(input);
        
        // parse UCI "quit" command
        else if (strncmp(input, "quit", 4) == 0)
            break;
        
        // parse UCI "uci" command
        else if (strncmp(input, "uci", 3) == 0)
        {
            // print engine info
            cout << "id name Thrawn"<< version << "\n";
            cout << "id author Feiyu Lin\n";
            cout << "option name Hash type spin default 128 min 4 max " << max_hashmap_size << "\n";
            cout << "uciok\n";
        }
        
        else if (!strncmp(input, "setoption name Hash value ", 26)) {			
            // init MB
            sscanf(input,"%*s %*s %*s %*s %d", &mb);
            
            // adjust MB if going beyond the aloowed bounds
            if(mb < 4) mb = 4;
            if(mb > max_hashmap_size) mb = max_hashmap_size;
            
            // set hash table size in MB
            std::cout << "    Set hash table size to " << mb << "MB\n";
            init_hashmap(mb);
        }
    }
}

void reset_time_control()
{
    quit = 0;
    movestogo = 30;
    movetime = -1;
    uci_time = -1;
    inc = 0;
    starttime = 0;
    stoptime = 0;
    timeset = 0;
    stopped = 0;
}
