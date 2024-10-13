#include "Common.h"

#include <filesystem>

#include "Containers/String.h"
#include "System/Win32.h"

#include "Tokenizer.h"

String StringFromFormat(const char* Format, ...);

int main()
{
    #if TRACY_ENABLE
    Sleep(600);
    #endif
    ZoneScopedN("generate_code kickoff");

	using recursive_directory_iterator = std::filesystem::recursive_directory_iterator;

    FILE* AllDeclarations = fopen("./generated/AllDeclarations.h", "w");
    StringView PragmaOnce("#pragma once\n");
    fwrite(PragmaOnce.data(), 1, PragmaOnce.size(), AllDeclarations);

    String DeclarationsInclude;
    for (const auto& DirEntry : recursive_directory_iterator("./src/"))
    {
        if (!DirEntry.is_directory())
        {
            std::filesystem::path Extension = DirEntry.path().extension();
            if (Extension == ".h" || Extension == ".cpp")
            {
                std::string FilePath = DirEntry.path().string();

                ZoneScoped;

                FILE* File = fopen(FilePath.c_str(), "rb");
                CHECK(File);
                fseek(File, 0, SEEK_END);
                size_t Size = ftell(File);
                char* Data = (char*)malloc(Size + 1);
                fseek(File, 0, SEEK_SET);
                fread(Data, 1, Size, File);
                Data[Size] = 0;
                fclose(File);

                String InputFile = String(FilePath.c_str());
                InputFile.erase(0, sizeof("./src"));

                u64 Pos = InputFile.find('\\', 0);
                while (Pos != String::npos)
                {
                    InputFile.replace(Pos, 1, 1, '/');
                    Pos = InputFile.find('\\', Pos);
                }

                ZoneNameF("%s", InputFile.c_str());

                String OutputFile = InputFile;
                if (Extension == ".h")
                {
                    u64 Index = OutputFile.find_last_of('.');
                    OutputFile.insert(Index, ".generated");

                    OutputFile.insert(0, "./generated/");
                    String DirPath = OutputFile.substr(0, OutputFile.find_last_of("\\/"));
                    if (!std::filesystem::exists(DirPath.c_str()))
                    {
                        std::filesystem::create_directories(DirPath.c_str());
                    }
                    ParseHeader(StringView(Data, Size), InputFile, OutputFile, AllDeclarations);
                }
                else
                {
                    u64 Index = OutputFile.find_last_of('.');
                    OutputFile.resize(Index);
                    OutputFile.append(".Declarations.h");

                    DeclarationsInclude += StringFromFormat("#include \"%.*s\"\n", VIEW_PRINT(OutputFile));

                    OutputFile.insert(0, "./generated/");
                    String DirPath = OutputFile.substr(0, OutputFile.find_last_of("\\/"));
                    if (!std::filesystem::exists(DirPath.c_str()))
                    {
                        std::filesystem::create_directories(DirPath.c_str());
                    }
                    ParseCode(StringView(Data, Size), InputFile, OutputFile);
                }

                free(Data);
            }
        }
    }

    fwrite(DeclarationsInclude.data(), 1, DeclarationsInclude.size(), AllDeclarations);
    fclose(AllDeclarations);
}