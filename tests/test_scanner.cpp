#include <cassert>
#include <iostream>
#include <algorithm>
#include <QCoreApplication>
#include <QJsonObject>
#include "../src/scanner.h"
#include "../src/common.h"

void test_parses_exif_capture_timestamp() {
    ParsedDateResult res = Scanner::parseCaptureDate("2025:02:03 04:05:06");
    assert(res.valid);
    assert(res.date == "2025-02-03");
    assert(res.time == "04:05:06");
    assert(res.timezone.isEmpty());
    std::cout << "test_parses_exif_capture_timestamp passed." << std::endl;
}

void test_parses_iso_timestamp_with_utc_offset() {
    ParsedDateResult res = Scanner::parseCaptureDate("2025-02-03T04:05:06+05:30");
    assert(res.valid);
    assert(res.date == "2025-02-03");
    assert(res.time == "04:05:06");
    assert(res.timezone == "+0530");
    std::cout << "test_parses_iso_timestamp_with_utc_offset passed." << std::endl;
}

void test_date_without_time_remains_date_only() {
    ParsedDateResult res = Scanner::parseCaptureDate("2025-02-03");
    assert(res.valid);
    assert(res.date == "2025-02-03");
    assert(res.time.isEmpty());
    assert(res.timezone.isEmpty());
    std::cout << "test_date_without_time_remains_date_only passed." << std::endl;
}

void test_invalid_preferred_timestamp_does_not_mask_valid_fallback() {
    QJsonObject meta;
    meta["EXIF:DateTimeOriginal"] = "0000:00:00 00:00:00";
    meta["QuickTime:CreateDate"] = "2025:02:03 04:05:06";

    auto pair = Scanner::findCaptureTag(meta);
    assert(pair.first == "QuickTime:CreateDate");
    assert(pair.second == "2025:02:03 04:05:06");
    std::cout << "test_invalid_preferred_timestamp_does_not_mask_valid_fallback passed." << std::endl;
}

void test_exififd_family1_tag_priority() {
    QJsonObject meta;
    meta["ExifIFD:DateTimeOriginal"] = "2025:02:03 04:05:06";
    meta["XMP-xmp:CreateDate"] = "2025:02:03 03:00:00";

    auto pair = Scanner::findCaptureTag(meta);
    assert(pair.first == "ExifIFD:DateTimeOriginal");
    assert(pair.second == "2025:02:03 04:05:06");
    std::cout << "test_exififd_family1_tag_priority passed." << std::endl;
}

void test_natural_sort_places_two_before_ten() {
    QStringList filenames = {"IMG_10.JPG", "IMG_2.JPG", "IMG_1.JPG"};
    std::sort(filenames.begin(), filenames.end(), naturalLess);

    assert(filenames[0] == "IMG_1.JPG");
    assert(filenames[1] == "IMG_2.JPG");
    assert(filenames[2] == "IMG_10.JPG");
    std::cout << "test_natural_sort_places_two_before_ten passed." << std::endl;
}

void test_order_matching_detection() {
    PhotoRecord r1, r2, r3;
    r1.source_path = "/path/photo1.jpg";
    r1.source_order = 1;

    r2.source_path = "/path/photo2.jpg";
    r2.source_order = 2;

    r3.source_path = "/path/photo3.jpg";
    r3.source_order = 3;

    QList<PhotoRecord> orig = {r1, r2, r3};
    QList<PhotoRecord> propMatching = {r1, r2, r3};
    QList<PhotoRecord> propDiffering = {r2, r1, r3};

    auto checkMatch = [](const QList<PhotoRecord> &o, const QList<PhotoRecord> &p) {
        if (o.size() != p.size()) return false;
        for (int i = 0; i < o.size(); ++i) {
            if (o[i].source_path != p[i].source_path) return false;
        }
        return true;
    };

    assert(checkMatch(orig, propMatching) == true);
    assert(checkMatch(orig, propDiffering) == false);
    std::cout << "test_order_matching_detection passed." << std::endl;
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    test_parses_exif_capture_timestamp();
    test_parses_iso_timestamp_with_utc_offset();
    test_date_without_time_remains_date_only();
    test_invalid_preferred_timestamp_does_not_mask_valid_fallback();
    test_exififd_family1_tag_priority();
    test_natural_sort_places_two_before_ten();
    test_order_matching_detection();
    std::cout << "\nAll 7 tests passed successfully!" << std::endl;
    return 0;
}
