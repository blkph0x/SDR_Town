#include "DeviceOwnership.h"
#include <catch2/catch_test_macros.hpp>

using Book = DeviceOwnership;
using Owner = Book::Owner;

TEST_CASE("Independent radios and repeated workflow instances do not steal leases", "[ownership]") {
    Book book;
    book.bind({{"rtl-a", "a"}, {"rtl-b", "b"}, {"rtl-c", "c"}, {"rtl-d", "d"}, {"rsp", "e"}});
    std::vector<Book::Token> tokens;
    for (size_t i = 0; i < 4; ++i) tokens.push_back(book.claim(i, Owner::P25, "p25-" + std::to_string(i)));
    const auto sstv = book.claim(4, Owner::Sstv, "sstv-1");
    REQUIRE(sstv);
    for (const auto& token : tokens) CHECK(book.valid(token));
    CHECK_FALSE(book.claim(4, Owner::P25, "p25-4"));
    CHECK_FALSE(book.claim(0, Owner::P25, "different-p25-session"));
    REQUIRE(book.release(tokens[0]));
    CHECK(book.valid(sstv));
    for (size_t i = 1; i < 4; ++i) CHECK(book.valid(tokens[i]));
    const auto next = book.claim(0, Owner::P25, "p25-0");
    CHECK_FALSE(book.release(tokens[0]));
    CHECK(book.valid(next));
    book.bind({{"rtl-b", "b"}, {"rtl-a", "a"}});
    CHECK_FALSE(book.valid(next));
    CHECK_FALSE(book.release(sstv));
}

TEST_CASE("Assignments survive reorder and never fall back to an unrelated radio", "[ownership]") {
    Book book;
    book.bind({{"a", "a"}, {"b", "b"}});
    REQUIRE(book.setAssignments({{"a", Owner::Sstv}, {"missing", Owner::P25}}, nullptr));
    CHECK_FALSE(book.claim(0, Owner::P25, "p25"));
    CHECK(book.claim(1, Owner::P25, "p25"));
    book.bind({{"b", "b"}, {"a", "a"}});
    CHECK(book.assignment(1) == Owner::Sstv);
    CHECK(book.assignments().count("missing") == 1);
    CHECK(Book::parse(Book::serialize(book.assignments())) == book.assignments());
    REQUIRE(book.claim(1, Owner::Sstv, "sstv"));
    CHECK_FALSE(book.setAssignments({}, nullptr));
    book.invalidate(1);
    CHECK(book.setAssignments({}, nullptr));
}

TEST_CASE("Duplicate serials and shared SDRplay domains fail closed", "[ownership]") {
    Book book;
    book.bind({{"same", "a"}, {"same", "b"}});
    CHECK_FALSE(book.unique(0));
    CHECK_FALSE(book.claim(0, Owner::Sstv, "sstv"));
    CHECK_FALSE(book.setAssignments({{"same", Owner::Sstv}}, nullptr));
    book.bind({{"duo-A", "duo-serial"}, {"duo-B", "duo-serial"}});
    CHECK_FALSE(book.setAssignments({{"duo-A", Owner::P25}, {"duo-B", Owner::Sstv}}, nullptr));
    REQUIRE(book.setAssignments({{"duo-A", Owner::P25}}, nullptr));
    CHECK_FALSE(book.claim(1, Owner::Sstv, "sstv"));
    REQUIRE(book.claim(0, Owner::P25, "p25"));
    CHECK_FALSE(book.claim(1, Owner::P25, "p25-2"));
}

TEST_CASE("Assignment persistence rejects unsupported input", "[ownership]") {
    CHECK_THROWS(Book::parse({{"schema", 2}, {"assignments", nlohmann::json::object()}}));
    CHECK_THROWS(Book::parse({{"schema", 1}, {"assignments", {{"a", "unknown"}}}}));
    CHECK_THROWS(Book::parse({{"schema", 1}, {"assignments", {{"", "p25"}}}}));
    Book book;
    book.bind({{"a", "a"}});
    CHECK_FALSE(book.claim(0, Owner::None, "caller"));
    CHECK_FALSE(book.claim(99, Owner::Sstv, "caller"));
    auto token = book.claim(0, Owner::Sstv, "caller");
    auto forged = token; forged.client = "other";
    CHECK_FALSE(book.release(forged));
    CHECK(book.valid(token));
}

TEST_CASE("Stopping protects the endpoint until driver teardown finishes", "[ownership]") {
    Book book;
    book.bind({{"a", "a"}, {"b", "b"}});
    const auto token = book.claim(0, Owner::Sstv, "sstv");
    REQUIRE(book.beginStop(0));
    book.invalidate(0);
    CHECK_FALSE(book.claim(0, Owner::P25, "p25"));
    CHECK(book.claim(1, Owner::Listen, "nfm"));
    CHECK_FALSE(book.release(token));
    book.endStop(0);
    CHECK(book.claim(0, Owner::P25, "p25"));
}

TEST_CASE("All workflow types use the same reservation contract", "[ownership]") {
    for (auto owner : {Owner::Listen, Owner::P25, Owner::Satcom, Owner::Inmarsat, Owner::Aircraft, Owner::Sstv}) {
        Book book;
        book.bind({{"a", "a"}, {"b", "b"}});
        REQUIRE(book.setAssignments({{"a", owner}, {"b", owner}}, nullptr));
        CHECK(book.claim(0, owner, "workflow-1"));
        CHECK(book.claim(1, owner, "workflow-2"));
        CHECK_FALSE(book.claim(0, owner, "different-instance"));
        CHECK_FALSE(book.setAssignments({{"a", Owner(99)}}, nullptr));
    }
}
