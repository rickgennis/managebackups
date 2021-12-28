#include "Setting.h"
#include "globals.h"


Setting::Setting(string name, string pattern, enum SetType setType, string defaultVal) {
    regex = Pcre(pattern + CAPTURE_VALUE + RE_COMMENT);
    display_name = name;
    data_type = setType;
    defaultValue = defaultVal;
    value = defaultValue;
    seen = false;
}

/*
string Setting::getValue(bool getDefault) {
    string result;
    seen = true;

    return std::visit([](auto&& arg) -> string {
            using T = std::decay_t<decltype(arg)>;

            // int
            if constexpr (std::is_same_v<T, int>)
                return to_string(arg);

            // string
            else if constexpr (std::is_same_v<T, std::string>)
                return(arg);

            // vector
            else if constexpr (std::is_same_v<T, std::vector<string>>) {
                auto vec = arg;
                string result;
                for (auto vec_it = vec.begin(); vec_it != vec.end(); ++vec_it) {
                    result += (result.length() ? ", " : "") + *vec_it;
                }
                return result;
            }

            return "";  // should never get here
    }, getDefault ? defaultValue : value);
}


void Setting::value = string newValue) {
    cout << "SETTING " << display_name << " [was {" << value << "}] to " << newValue << endl;

    switch (data_type) {
        case INT: {
            int tempVal = stoi(newValue);
            value = tempVal;
            break;
        }

        case STRING:
        default:
            value = newValue;
            break;

        case VECTOR: {
            vector<string> vec;
            Pcre reDelimiters("\\s*[,;]\\s*", "g");
            auto values = reDelimiters.split(newValue);

            for (auto str_it = values.begin(); str_it != values.end(); ++str_it)
                vec.insert(vec.end(), *str_it);

            value = vec;
            break;
        }
    }
}


void Setting::value = int newValue) {   // convenience func
    cout << "SETTING (int)" << display_name << " [was {" << value << "}] to " << newValue << endl;
    assert(data_type == INT);
    value = newValue;

}
*/

