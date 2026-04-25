/*
    Sourced with Access Date 20.04.2026:
    https://github.com/JulianNeeleman/pizza/blob/master/tools/validator.cpp

    Compile:
    g++ -o validator validator.cpp

    Run:
    ./validator <input_file> <result_file>
*/
#include <iostream>
#include <fstream>

using namespace std;

int r1, r2, c1, c2;

/**
 *  Print the dimensions of the slice in human-readable form.
 */
void print_slice(const int s) {
    cout    << "Slice " << s << " between rows ("
            << r1 << "," << r2 << ") and columns ("
            << c1 << "," << c2 << "): ";
}

int main(int argc, char *argv[])
{
    // Check if the number of arguments is correct.
    if (argc != 3) {
        cout    << "Expected 2 arguments, got " << argc - 1 << "." << endl
                << "Usage: ./validator <input filename> <result filename>" << endl;
        return -1;
    }

    // Attempt to open the files.
    ifstream    input_file(argv[1]),
                result_file(argv[2]);

    // Check if opening files succeeded.
    if (!input_file.is_open()) {
        cout << "Input file " << argv[1] << " does not exist." << endl;
        return -1;
    }

    if (!result_file.is_open()) {
        cout << "Result file " << argv[1] << " does not exist." << endl;
        return -1;
    }

    // Read in the input parameters.
    int R, C, L, H;
    char g[1000][1000];

    input_file >> R >> C >> L >> H;

    for (int i = 0; i < R; i++) {
        for (int j = 0; j < C; j++) {
            input_file >> g[i][j];
        }
    }

    // We are done with the input file.
    input_file.close();

    // Read in result file.
    int S, total = 0;
    result_file >> S;

    for (int s = 0; s < S; s++) {
        // Read in a slice.
        result_file >> r1 >> c1 >> r2 >> c2;

        // Check if the area of the slice does not
        // exceed the limit.
        int area = (r2 - r1 + 1) * (c2 - c1 + 1);
        if (area > H) {
            print_slice(s);
            cout << "area " << area << " larger than H (" << H << ")." << endl;
            return -1;
        }

        // Check if the slice is contained in the pizza.
        for (const int r : { r1, r2 }) {
            if (r < 0 || r >= R) {
                print_slice(s);
                cout << "out of bounds (" << R << "," << C << ")." << endl;
                return -1;
            }
        }

        for (const int c : { c1, c2 }) {
            if (c < 0 || c >= C) {
                print_slice(s);
                cout << "out of bounds (" << R << "," << C << ")." << endl;
                return -1;
            }
        }

        // Count the number of mushrooms and tomatoes
        // on this slice.
        int M = 0, T = 0;

        // Check for collisions and count.
        for (int i = r1; i <= r2; i++) {
            for (int j = c1; j <= c2; j++) {
                switch(g[i][j]) {
                    case 'M': M++; break;
                    case 'T': T++; break;
                    case 'U': {
                        print_slice(s);
                        cout << "collision with other slice in result." << endl;
                        return -1;
                    }
                }
            }
        }

        if (M < L) {
            print_slice(s);
            cout << L << " mushrooms required, but only " << M << " found." << endl;
            return -1;
        }

        if (T < L) {
            print_slice(s);
            cout << L << " tomatoes required, but only " << T << " found." << endl;
            return -1;
        }

        total += M + T;
    }

    cout << "Validation successful, score is " << total << ", coverage is " << (double)total / (double)(R * C) << endl;

    return 0;
}