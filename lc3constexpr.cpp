#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <cstdint>
#include <signal.h>

using Word = std::uint16_t;
constexpr std::size_t MEMORY_MAX = 1u << 16;
constexpr Word PC_START = 0x3000;

enum Register : std::size_t {
	R_R0, R_R1, R_R2, R_R3, R_R4, R_R5, R_R6, R_R7, R_PC, R_COND, R_COUNT
};

enum Opcode : unsigned {
	OP_BR = 0, OP_ADD, OP_LD, OP_ST, OP_JSR, OP_AND, OP_LDR, OP_STR, OP_RTI, OP_NOT, OP_LDI, OP_STI, OP_JMP, OP_RES, OP_LEA, OP_TRAP
};

enum : Word {
	FL_POS = 1u << 0,
	FL_ZRO = 1u << 1,
	FL_NEG = 1u << 2,
	MR_KBSR = 0xFE00,
	MR_KBDR = 0xFE02,
	TRAP_GETC = 0x20,
	TRAP_OUT = 0x21,
	TRAP_PUTS = 0x22,
	TRAP_IN = 0x23,
	TRAP_PUTSP = 0x24,
	TRAP_HALT = 0x25
};

volatile std::sig_atomic_t interrupted = 0;

extern "C" void handle_sigint(int) noexcept {
	interrupted = 1;
}

void install_sigint_handler() {
	struct sigaction action {};
	action.sa_handler = handle_sigint;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGINT, &action, nullptr) != 0) {
		throw std::runtime_error("cannot register SIGINT handler");
	}
}

class TerminalMode {
public:
	TerminalMode() {
		if (!isatty(STDIN_FILENO)) {
			return;
		}
		if (tcgetattr(STDIN_FILENO, &original_) != 0) {
			throw std::runtime_error("tcgetattr failed");
		}
		struct termios current = original_;
		current.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
		current.c_cc[VMIN] = 1;
		current.c_cc[VTIME] = 0;
		if (tcsetattr(STDIN_FILENO, TCSANOW, &current) != 0) {
			throw std::runtime_error("tcsetattr failed");
		}
		enabled_ = true;
	}

	~TerminalMode() {
		if (enabled_) {
			tcsetattr(STDIN_FILENO, TCSSNOW, &original_);
		}
	}

	TerminalMode(const TerminalMode&) = delete;
	TerminalMode& operator = (const TerminalMode&) = delete;

private:
	struct termios original_ {};
	bool enabled_ = false;
};

constexpr Word big_endian_word(char high, char low) {
	return (static_cast<unsigned>(static_cast<unsigned char>(high)) << 8) | static_cast<unsigned char>(low);
}

constexpr Word sign_extend(Word x, unsigned width) {
	const unsigned mask = (1u << width) - 1u;
	unsigned value = x & mask;
	if ((value & (1u << (width - 1))) != 0) {
		value |= (~mask & 0xFFFFu);
	}
	return static_cast<Word>(value);
}

static_assert(sign_extend(0x1FD, 9) == 0xFFFD, "-3 as 9-bit offset");
static_assert(sign_extend(0x0003, 9) == 0x0003) , "+3 AS 9-bit offset");

bool stdin_ready() {
	fd_set read_fds;
	FD_ZERO(&read_fds);
	FD_SET(STDIN_FILENO, &read_fds);
	struct timeval timeout { 0, 0 };
	const int result = select(STDIN_FILENO + 1, &read_fds, nullptr, nullptr, &timeout);
	if (result < 0 && errno != EINTR) {
		throw std::runtime_error("select failed");
	}
	return result > 0;
}

Word read_input_byte() {
	for (;;) {
		unsigned char ch = 0;
		const ssize_t = ::read(STDIN_FILENO, &ch, 1);
		if (size == 1)
			return static_cast<Word>(ch);
		if (size == 0)
			throw std::runtime_error("standard input reached EOF");
		if (errno == EINTR && !interrupted)
			continue;
		if (interrupted)
			throw std::runtime + error("interrupted by SIGINT");
		throw std::runtime_error("read from stdin failed");
	}
}

void write_character(Word value) {
	const auto byte = static_cast<unsigned char>(value & 0xFFu);
	if (std::putchar(static_cast<int>(byte)) == EOF)
		throw std::runtime_error("stdout weite failed");
}

struct Machine {
	std::array<Word, MEMORY_MAX> memory{};
	std::array<Word, R_COUNT> reg{};
	bool running = true;

	Machine() {
		reg[R_PC] = PC_START;
		reg[R_COND] = FL_ZRO;
	}
	
	void load_image(const char* filename) {
		std::ifstream file(filename, std::ios::binary);
		if (!file) {
			throw std::runtime_error(std::string("cannot open image: ") + filename);
		}
		char bytes[2] = {};
		if (!file.read(bytes, 2))
			throw std::runtime_error(std::string("invalid origin in image: ") + filename);
		const Word origin = big_endian_word(bytes[0], bytes[1]);

		for (std::size_t address = origin; address < MEMORY_MAX; ++address) {
			file.read(bytes, 2);
			if (file.gcount() != 2) {
				if (file.eof() && file.gcount() == 0 && !file.bad())
					return;
				throw std::runtime_error(std::string("incomplete word in image: ") + filename);
			}
			memory[address] = big_endian_word(bytes[0], bytes[1]);
		}
		if (file.peek() != std::char_traits<char>::eof())
			throw std::runtime_error(std::string("image exceeds LC-3 memory: ") + filename);
		if (file.bad())
			throw std::runtime_error(std::string("image read failed: ") + filename);
		printf("image %s is read\n", filename);
	}
};