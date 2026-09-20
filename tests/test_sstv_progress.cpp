#include "SstvProgress.h"
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace {
QByteArray row(int y,int image=0) {
    return QByteArray::fromStdString(nlohmann::json{{"kind","row"},{"schema",1},{"image",image},
        {"mode","robot36"},{"width",320},{"height",240},{"row",y},{"rgb",std::string(1920,'a')}}.dump()+"\n");
}
QByteArray end(int rows,bool complete=false) {
    return QByteArray::fromStdString(nlohmann::json{{"schema",1},{"file","image-0.rgb"},
        {"mode","robot36"},{"width",320},{"height",240},{"rows",rows},{"complete",complete}}.dump()+"\n");
}
}
TEST_CASE("SSTV scanline transport is chunk independent and preserves row positions", "[sstv-progress]") {
    for(int chunk:{1,19,4096,8192}) {
        int updates=0;
        SstvProgress stream([&](const auto&,const auto&,int){++updates;});
        QByteArray bytes=row(1)+row(0)+end(2);
        for(qsizetype i=0;i<bytes.size();i+=chunk) stream.append(bytes.mid(i,chunk));
        REQUIRE_NOTHROW(stream.finish()); REQUIRE(updates==1);
        REQUIRE(stream.images().size()==1);
        CHECK(stream.images()[0].pixelColor(0,0)==QColor(170,170,170));
        CHECK(stream.images()[0].pixelColor(0,1)==QColor(170,170,170));
        CHECK(stream.images()[0].pixelColor(0,2)==Qt::black);
        CHECK(stream.metadata()==end(2).trimmed()+'\n');
    }
}
TEST_CASE("SSTV preview rejects malformed or inconsistent rows", "[sstv-progress]") {
    auto sink=[](const auto&,const auto&,int){};
    SECTION("duplicate") {SstvProgress s(sink); s.append(row(0)); CHECK_THROWS(s.append(row(0)));}
    SECTION("wrong image") {SstvProgress s(sink); CHECK_THROWS(s.append(row(0,1)));}
    SECTION("bad row") {SstvProgress s(sink); CHECK_THROWS(s.append(row(240)));}
    SECTION("bad RGB") {SstvProgress s(sink); auto r=row(0); r.replace(std::string(1920,'a').c_str(),"x"); CHECK_THROWS(s.append(r));}
    SECTION("incomplete record") {SstvProgress s(sink); s.append("{"); CHECK_THROWS(s.finish());}
    SECTION("incomplete image") {SstvProgress s(sink); s.append(row(0)); CHECK_THROWS(s.finish());}
    SECTION("missing rows") {SstvProgress s(sink); s.append(row(0)); CHECK_THROWS(s.append(end(1,true)));}
    SECTION("bad row count") {SstvProgress s(sink); s.append(row(0)); CHECK_THROWS(s.append(end(2)));}
    SECTION("line bound") {SstvProgress s(sink); CHECK_THROWS(s.append(QByteArray(16385,'x')));}
    SECTION("transport bound") {SstvProgress s(sink); CHECK_THROWS(s.append(QByteArray(4*1024*1024+1,'\n')));}
    SECTION("image bound") {
        SstvProgress s(sink);
        for(int i=0;i<4;++i) {
            s.append(row(0,i));
            auto e=end(1); e.replace("image-0.rgb",QByteArray("image-")+QByteArray::number(i)+".rgb"); s.append(e);
        }
        CHECK_THROWS(s.append(row(0,4)));
    }
}

TEST_CASE("SSTV zero-row partial stays partial and black", "[sstv-progress]") {
    SstvProgress s([](const auto&,const auto&,int){});
    s.append(end(0)); REQUIRE_NOTHROW(s.finish());
    REQUIRE(s.images().size()==1); CHECK(s.images()[0].pixelColor(0,0)==Qt::black);
}
