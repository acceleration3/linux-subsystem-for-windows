#include "application.hpp"

int main(int argc, char** argv) {
    return application::instance().run(argc, argv);
}