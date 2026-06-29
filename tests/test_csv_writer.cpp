#include <doctest/doctest.h>

#include "profile/csv_writer.hpp"
#include "test_util.hpp"

TEST_CASE("writes header and rows") {
    const std::string path = tmpPath("out.csv");
    {
        CsvWriter writer(path);
        writer.writeHeader();
        writer.writeRow("Session One", 0, 100, 200, 1, AccessType::Read);
        writer.writeRow("Session One", 1, 300, 450, 8, AccessType::Write);
    }

    const std::string content = readTextFile(path);
    CHECK(content ==
          "Session Name,Statement Index,Time Start,Time End,Page Number,"
          "Read or Write\n"
          "Session One,0,100,200,1,Read\n"
          "Session One,1,300,450,8,Write\n");
}

TEST_CASE("escapes fields containing commas and quotes") {
    const std::string path = tmpPath("out_escaped.csv");
    {
        CsvWriter writer(path);
        writer.writeRow(R"(weird,"name)", 0, 1, 2, 3, AccessType::Read);
    }

    const std::string content = readTextFile(path);
    CHECK(content == "\"weird,\"\"name\",0,1,2,3,Read\n");
}
