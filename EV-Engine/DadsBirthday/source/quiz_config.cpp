#include "quiz_config.h"

#include <algorithm>
#include <cwctype>
#include <fstream>

#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace Birthday
{
    namespace
    {
        const char* g_defaultIntro =
            "Happy birthday! Walk with W A S D, look around with the mouse and shoot with the left mouse button. "
            "Turn around and shoot the birthday cake to start the quiz!";
        const char* g_defaultCorrect = "Goedzo Meneer Hyman!.";
        const char* g_defaultWrong = "Hmm.. volgends mij weet je beter...";

        std::string Text(const json& object, const char* key, const std::string& fallback = {})
        {
            const auto it = object.find(key);
            return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
        }

        std::filesystem::path PathFromUTF8(const std::string& utf8)
        {
            return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        }

        // Every picture in a folder, numbered names in number order (2.jpg before 10.jpg), then the rest by name.
        std::vector<std::filesystem::path> PicturesInFolder(const std::filesystem::path& folder)
        {
            std::vector<std::filesystem::path> pictures;
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(folder, error))
            {
                std::wstring extension = entry.path().extension().wstring();
                for (auto& c : extension)
                {
                    c = static_cast<wchar_t>(towlower(c));
                }
                if (entry.is_regular_file() &&
                    (extension == L".jpg" || extension == L".jpeg" || extension == L".png" || extension == L".webp"))
                {
                    pictures.push_back(entry.path());
                }
            }
            auto number = [](const std::filesystem::path& path)
            {
                const std::wstring stem = path.stem().wstring();
                const bool numbered = !stem.empty() && stem.find_first_not_of(L"0123456789") == std::wstring::npos;
                return numbered ? std::stoull(stem) : ~0ull;
            };
            std::sort(pictures.begin(), pictures.end(), [&number](const auto& a, const auto& b)
            {
                const auto numberA = number(a);
                const auto numberB = number(b);
                return numberA != numberB ? numberA < numberB : a.filename() < b.filename();
            });
            return pictures;
        }
    }

    QuizConfig LoadQuizConfig(const std::filesystem::path& file)
    {
        QuizConfig config;
        config.title = "Happy Birthday!";
        config.intro = g_defaultIntro;
        config.finaleTitle = "Happy Birthday!";
        config.finaleMessage = "That was the last question. Happy birthday!";

        const std::filesystem::path folder = file.parent_path();
        json root;
        try
        {
            std::ifstream stream(file, std::ios::binary);
            if (!stream)
            {
                config.warnings.push_back("Could not open " + file.string());
                return config;
            }
            // Comments are allowed, so the file can explain itself.
            root = json::parse(stream, nullptr, true, true);
        }
        catch (const std::exception& e)
        {
            config.warnings.push_back(file.filename().string() + " has a mistake in it: " + e.what());
            return config;
        }

        config.title = Text(root, "title", config.title);
        config.intro = Text(root, "intro", config.intro);

        const auto questions = root.find("questions");
        if (questions != root.end() && questions->is_array())
        {
            int number = 0;
            for (const auto& entry : *questions)
            {
                ++number;
                if (!entry.is_object())
                {
                    continue;
                }
                QuizQuestion question;
                question.question = Text(entry, "question");
                question.leftAnswer = Text(entry, "left");
                question.rightAnswer = Text(entry, "right");
                const std::string correct = Text(entry, "correct", "left");
                if (correct != "left" && correct != "right")
                {
                    config.warnings.push_back("Question " + std::to_string(number) + ": \"correct\" must be \"left\" or \"right\"");
                }
                question.correctSide = correct == "right" ? Side::Right : Side::Left;

                auto image = [&](const char* key)
                {
                    const std::string path = Text(entry, key);
                    return path.empty() ? std::filesystem::path() : folder / PathFromUTF8(path);
                };
                question.correctImage = image("correctImage");
                question.wrongImage = image("wrongImage");
                question.correctImage2 = image("correctImage2");
                question.wrongImage2 = image("wrongImage2");
                if (question.correctImage2.empty())
                {
                    question.correctImage2 = question.correctImage;
                }
                if (question.wrongImage2.empty())
                {
                    question.wrongImage2 = question.wrongImage;
                }
                question.correctMessage = Text(entry, "correctMessage", g_defaultCorrect);
                question.wrongMessage = Text(entry, "wrongMessage", g_defaultWrong);

                if (question.question.empty())
                {
                    config.warnings.push_back("Question " + std::to_string(number) + " has no \"question\"");
                }
                config.questions.push_back(std::move(question));
            }
        }
        if (config.questions.empty())
        {
            config.warnings.push_back(file.filename().string() + " has no questions");
        }

        const auto finale = root.find("finale");
        if (finale != root.end() && finale->is_object())
        {
            config.finaleTitle = Text(*finale, "title", config.finaleTitle);
            config.finaleMessage = Text(*finale, "message", config.finaleMessage);
            const auto images = finale->find("images");
            if (images != finale->end() && images->is_array())
            {
                for (const auto& image : *images)
                {
                    if (!image.is_string())
                    {
                        continue;
                    }
                    const std::filesystem::path path = folder / PathFromUTF8(image.get<std::string>());
                    if (std::filesystem::is_directory(path))
                    {
                        const auto pictures = PicturesInFolder(path);
                        if (pictures.empty())
                        {
                            config.warnings.push_back("finale: no pictures in " + image.get<std::string>());
                        }
                        config.finaleImages.insert(config.finaleImages.end(), pictures.begin(), pictures.end());
                    }
                    else
                    {
                        config.finaleImages.push_back(path);
                    }
                }
            }
        }
        if (config.finaleImages.empty())
        {
            for (const auto& question : config.questions)
            {
                if (!question.correctImage.empty())
                {
                    config.finaleImages.push_back(question.correctImage);
                }
            }
        }
        return config;
    }
}
