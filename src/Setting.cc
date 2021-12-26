#include "Setting.h"
#include "globals.h"


Setting::Setting(string name, string pattern, enum SetType setType, SETTINGVARIANT defaultVal) {
    regex = Pcre(pattern + CAPTURE_VALUE + RE_COMMENT);
    display_name = name;
    data_type = setType;
    default_value = defaultVal;
    data_value = default_value;
    seen = false;
}


string Setting::getValue(bool getDefault) {
    string result;
    seen = true;

    return std::visit([](auto&& arg) -> string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, int>)
                return to_string(arg);
            else if constexpr (std::is_same_v<T, std::string>)
                return(arg);
            else if constexpr (std::is_same_v<T, std::vector<string>>) {
                auto vec = arg;
                string result;
                for (auto vec_it = vec.begin(); vec_it != vec.end(); ++vec_it) {
                    result += (result.length() ? ", " : "") + *vec_it;
                }
                return result;
            }
            return "";  // should never get here
    }, getDefault ? default_value : data_value);
}


void Setting::setValue(string newValue) {
    switch (data_type) {
        case INT: {
            int tempVal = stoi(newValue);
            data_value = tempVal;
            break;
        }

        case STRING:
        default:
            data_value = newValue;
            break;

        case VECTOR: {
            vector<string> vec;
            Pcre reDelimiters("\\s*[,;]\\s*", "g");
            auto values = reDelimiters.split(newValue);

            for (auto str_it = values.begin(); str_it != values.end(); ++str_it)
                vec.insert(vec.end(), *str_it);

            data_value = vec;
            break;
        }
    }
}


void Setting::setValue(int newValue) {   // convenience func
    assert(data_type == INT);
    data_value = newValue;
}

