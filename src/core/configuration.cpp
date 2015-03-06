# include <rdsn/internal/configuration.h>
# include <rdsn/internal/logging.h>
# include <cassert>
# include <rdsn/internal/utils.h>
# include <errno.h>

namespace rdsn {

configuration::configuration(const char* file_name)
{
    _file_name = std::string(file_name);

    FILE* fd = ::fopen(file_name, "rb");
    if (fd == nullptr) 
    {
        printf("Cannot open file %s, err=%s", file_name, strerror(errno));
        return;
    }
    ::fseek(fd, 0, SEEK_END);
    int len = ftell(fd);
    if (len == -1 || len == 0) 
    {
        printf("Cannot get length of %s, err=%s", file_name, strerror(errno));
        fclose(fd);
        return;
    }

    int fileLength = len;
    _file_data.reset((char*)malloc(len+1));
    char* fileData = _file_data.get();

    ::fseek(fd, 0, SEEK_SET);
    ::fread(fileData, len, 1, fd);
    ::fclose(fd);
    ((char*)fileData)[fileLength] = '\n';

    //
    // parse mapped file and build conf map
    //
    std::map<std::string, conf>* pSection = nullptr;
    char *p, *pLine = (char*)"", *pNextLine, *pEnd, *pSectionName = nullptr, *pEqual;
    int lineno = 0;
    unsigned int indexInSection = 0;

    p = (char*)fileData;
    pEnd = p + fileLength;

    while (p < pEnd) {
        //
        // get line
        //
        lineno++;
        while (*p == ' ' || *p == '\t' || *p == '\r')    p++;

        pLine = p;
        int shift = 0;
        while (*p != '\n' && p < pEnd)    
        {
            if (*p == '#' || *p == ';')
            {
                if (p != pLine && *(p-1) == '^')
                {
                    shift++;
                }
                else
                {
                    *p = '\0';
                }
            }

            if (shift > 0)
            {
                *(p-shift) = *p;
            }
            p++;
        }
        *(p-shift) = '\0';
        pNextLine = ++p;

        //
        // parse line
        //
        p = pLine;
        if (*p == '\0')    goto Next;    // skip comment line or empty line
        pEqual = strchr(p, '=');
        if (nullptr == pEqual && *p != '[') {
            goto ConfReg;
        }
        if (nullptr != pEqual && *p == '[') goto err;

        //
        //    conf
        //
        if (pEqual) 
        {
ConfReg:
            if (pSection == nullptr) {
                printf("configuration section not defined");
                goto err;
            }
            if (pEqual)    *pEqual = '\0';
            char* pKey = utils::trim_string(p);
            char* pValue = pEqual ? utils::trim_string(++pEqual) : nullptr;
            if (*pKey == '\0')    goto err;

            if (pSection->find((const char*)pKey) != pSection->end()) 
            {
                auto it = pSection->find((const char*)pKey);

                printf("Warning: skip redefinition of option [%s] %s (line %u), already defined as [%s] %s (line %u)\n", 
                    pSectionName,
                    pKey,
                    lineno,
                    it->second.section,
                    it->second.key,
                    it->second.line
                    );
            }
            else
            {
                conf cf;
                cf.section = (const char*)pSectionName;
                cf.key = (const char*)pKey;
                cf.value = pValue;
                cf.line = lineno;
                pSection->insert(std::make_pair(std::string(pKey), cf));
            }            
        }
        //
        //    section
        //
        else 
        {
            char* pRight = strchr(p, ']');
            if (nullptr == pRight)    goto err;
            *pRight = '\0';
            p++;
            pSectionName = utils::trim_string(p);
            if (*pSectionName == '\0')    goto err;

            if (has_section((const char*)pSectionName)) {
                printf("RedefInition of section %s", pSectionName);
                goto err;
            }

            std::map<std::string, conf> sm;
            auto it = _configs.insert(config_map::value_type(std::string(pSectionName), sm));
            assert (it.second);
            pSection = &it.first->second;
            indexInSection = 0;
        }

        //
        // iterate nextline
        //
Next:
        p = pNextLine;
    }
    
err:
    printf("Unexpected configure in %s(line %d): %s\n", file_name, lineno, pLine);
}

configuration::~configuration(void)
{
}

void configuration::get_all_sections(std::vector<std::string>& sections)
{
    for (auto it = _configs.begin(); it != _configs.end(); it++)
    {
        sections.push_back(it->first);
    }
}

void configuration::get_all_keys(const char* section, std::vector<std::string>& keys)
{
    auto it = _configs.find(section);
    if (it != _configs.end())
    {
        for (auto it2 = it->second.begin(); it2 != it->second.end(); it2++)
        {
            keys.push_back(it2->first);
        }
    }
}

std::string configuration::get_string_value(const char* section, const char* key, const char* default_value)
{
    auto it = _configs.find(section);
    if (it != _configs.end())
    {
        auto it2 = it->second.find(key);
        if (it2 != it->second.end())
        {
            return it2->second.value;
        }
    }

    return default_value;
}

std::list<std::string> configuration::get_string_value_list(const char* section, const char* key, char splitter)
{
    std::list<std::string> vs;
    std::string v = get_string_value(section, key, "");
    utils::split_args(v.c_str(), vs, splitter);

    for (auto& v : vs)
    {
        v = std::string(utils::trim_string((char*)v.c_str()));
    }
    return vs;
}

void configuration::register_config_change_notification(config_file_change_notifier notifier)
{
    rdsn_assert(false, "not implemented");
}

bool configuration::has_section(const char* section)
{
    auto it = _configs.find(section);
    return (it != _configs.end());
}

bool configuration::has_key(const char* section, const char* key)
{
    auto it = _configs.find(section);
    if (it != _configs.end())
    {
        auto it2 = it->second.find(key);
        return (it2 != it->second.end());
    }
    return false;
}

}