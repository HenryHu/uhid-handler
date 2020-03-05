#include <usbhid.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <vector>
#include <map>
#include <string>
#include <utility>

using ValueChangeHandler = std::function<void(const hid_item_t& item, int32_t prev_value, int32_t value)>;

constexpr hid_kind_t kInterestedKind = hid_input;
constexpr uint32_t HIO_CONST = 1;

void SendKeycode(const std::string& keycode) {
    system(("xdotool key " + keycode).c_str());
}

ValueChangeHandler KeyMappingHandler(const std::string& keyname) {
    return [keyname](const hid_item_t& item, int32_t prev_value, int32_t value) {
        if (value == 1 && prev_value == 0) {
            SendKeycode(keyname);
        }
    };
}

struct HidItem {
    hid_item_t item;
    int32_t value;
};

using HidItems = std::map<int32_t, std::vector<HidItem>>;

HidItems ParseHidReportDesc(report_desc_t report_desc) {
    HidItems hid_items;
    hid_data_t hid_data = hid_start_parse(report_desc, 1 << kInterestedKind, -1);
    while (true) {
        hid_item_t hid_item;

        int result = hid_get_item(hid_data, &hid_item);
        if (result == 0) break;
        if (result < 0) {
            perror("Fail to parse report descriptor.");
            return {};
        }

        if (hid_item.flags & HIO_CONST) continue;
        if (hid_item.kind != kInterestedKind) continue;
        printf("%20s %40s Logical: %d-%d Physical: %d-%d Report: %dx%d Flags: %x\n", hid_usage_page(HID_PAGE(hid_item._usage_page)),
                hid_usage_in_page(hid_item.usage), hid_item.logical_minimum, hid_item.logical_maximum,
                hid_item.physical_minimum, hid_item.physical_maximum, hid_item.report_size, hid_item.report_count,
                hid_item.flags);
        hid_items[hid_item.report_ID].push_back({std::move(hid_item), 0});
    }
    hid_end_parse(hid_data);
    return hid_items;
}

int main(int argc, char **argv) {
    const std::map<std::pair<std::string, std::string>, ValueChangeHandler> handlers = {
        {{"Consumer", "Volume_Increment"},    KeyMappingHandler("XF86AudioRaiseVolume")},
        {{"Consumer", "Volume_Decrement"},    KeyMappingHandler("XF86AudioLowerVolume")},
        {{"Consumer", "Mute"},                KeyMappingHandler("XF86AudioMute")},
        {{"Consumer", "Stop"},                KeyMappingHandler("XF86AudioStop")},
        {{"Consumer", "Play/Pause"},          KeyMappingHandler("XF86AudioPlay")},
        {{"Consumer", "Scan_Next_Track"},     KeyMappingHandler("XF86AudioNext")},
        {{"Consumer", "Scan_Previous_Track"}, KeyMappingHandler("XF86AudioPrev")},
    };

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <uhid device>\n", argv[0]);
        return 1;
    }

    hid_init(nullptr);

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("Cannot open specified device.");
        return 1;
    }

    const bool use_report_id = (hid_get_report_id(fd) != 0);
    const report_desc_t report_desc = hid_get_report_desc(fd);
    HidItems hid_items = ParseHidReportDesc(report_desc);
    if (hid_items.empty()) {
        fprintf(stderr, "No HID item.");
        return 1;
    }
    hid_dispose_report_desc(report_desc);

    char buf[1024];
    while (true) {
        int len = read(fd, buf, sizeof(buf));
        if (len < 0) break;
        int report_id;
        if (use_report_id) {
            report_id = buf[0];
        } else {
            report_id = NO_REPORT_ID;
        }
        const int report_size = hid_report_size(report_desc, hid_input, report_id);
        if (len != report_size) {
            fprintf(stderr, "report size not expected: %d != %d\n", len, report_size);
        }
        for (HidItem& hid_item : hid_items[report_id]) {
            const hid_item_t& item = hid_item.item;
            int32_t value = hid_get_data(buf, &item);
            if (value != hid_item.value) {
                // printf("%s = %d\n", hid_usage_in_page(item.usage), value);
                const auto found = handlers.find(
                        {hid_usage_page(HID_PAGE(item.usage)), hid_usage_in_page(item.usage)});
                if (found != handlers.end()) {
                    found->second(item, hid_item.value, value);
                }
            }
            hid_item.value = value;
        }
    }
}
