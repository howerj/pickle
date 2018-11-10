#include <iostream>
#include <string>
#include "pickle.hpp"

using namespace std;

int main(void) {
	auto p = Pickle();
	string line;
	p.set("prompt", "pickle> ");
	cout << "Pickle Interpreter: C++ test application." << endl;

	while (!cin.eof()) {
		auto prompt = p.get("prompt");
		if (prompt != nullptr) {
			cout << *prompt;
			delete prompt;
		}
		getline(cin, line);
		int r = 0;
		auto rs = p.eval(line, r);
		if (rs != nullptr)
			cout << "[" << r << "] " << *rs << endl;
		else
			return -1;
		delete rs;
	}
	return 0;
}

