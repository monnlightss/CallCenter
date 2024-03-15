#define _CRT_SECURE_NO_WARNINGS
#define SQLITE_ENABLE_COLUMN_METADATA
#include <iostream>
#include <locale>
#include <codecvt>
#include <string>
#include <vector>
#include <array>
#include <iomanip> 
#include <algorithm>
#include <sstream>
#include <SFML/Graphics.hpp>
#include "imgui.h"
#include "imgui-SFML.h"
#include "sqlite3.h"
#include <SQLiteCpp/SQLiteCpp.h>

namespace SQLite {
    const int OPEN_READWRITE = 0x00000002;
    const int OPEN_CREATE = 0x00000004;
}
const std::string DB_FILE = "CallCenterClients.db";
SQLite::Database* globalDb;

std::vector<std::string> currentTaskInfo;
bool isInCall = false;
constexpr size_t MaxTasks = 100;
std::vector<bool> taskCompletedStatus;

void ExecuteQuery(const std::string& query);
void ConnectToDatabase();
void CloseDatabaseConnection();
void ShowLoginForm(bool& showLoginForm, std::string& username, std::string& password, bool& loginError, std::vector<std::vector<std::string>>& clientInfo, int& operatorID);
void ShowMainInterface(std::vector<std::vector<std::string>>& clientInfo, bool& showLoginForm, bool& showTaskForm, int& selectedTaskID, bool& needToReloadData, const std::string& username);
void FetchClientInformation(const std::string& username, const std::string& password, std::vector<std::vector<std::string>>& result, int& operatorID);
void ShowTaskForm(const std::vector<std::string>& taskInfo, bool& showLoginForm, bool& showTaskForm, bool& showCallForm, int& selectedTaskID, bool& showCallCompletionForm);
void ShowCallForm(const std::vector<std::string>& taskInfo, bool& showCallForm, bool& showCallCompletionForm, int& callTimer, std::string& beginningTime, std::string& endTimeStr);
void ShowCallCompletionForm(const std::vector<std::string>& taskInfo, bool& showCallForm, bool& showCallCompletionForm, bool& showTaskForm, bool& showInfoWindow, int& callTimer, std::string& beginningTime, std::string& endTimeStr);
void ShowInfoWindow(const std::vector<std::string>& taskInfo, bool& closeInfoWindow, bool& showMainForm);
void MarkTaskAsCompleted(int taskID);
void LoadTaskCompletedStatus();
std::string GetOperatorFullName(const std::string& currentUsername);

int main() {
    setlocale(LC_ALL, "ru_RU.UTF-8");

    sf::RenderWindow window(sf::VideoMode(1000, 600), "Call Center App", sf::Style::Titlebar | sf::Style::Close);
    window.setFramerateLimit(60);

    ImGui::SFML::Init(window);

    ConnectToDatabase();

    bool showLoginForm = true;
    bool showTaskForm = false;
    bool showCallForm = false;
    bool dataFetched = false;
    bool showCallCompletionForm = false;
    bool loginError = false;
    bool showInfoWindow = false;
    bool closeInfoWindow = false;
    bool showMainForm = false;
    bool needToReloadData = true;
    int selectedTaskID = -1;
    int operatorID = -1;

    std::string username;
    std::string password;

    int callTimer = 0;
    std::string beginningTime;
    std::string endTimeStr;

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    io.Fonts->AddFontFromFileTTF("Roboto-Black.ttf", 12.f, NULL,
        ImGui::GetIO().Fonts->GetGlyphRangesCyrillic());

    ImGui::SFML::UpdateFontTexture();

    std::vector<std::vector<std::string>> clientInfo;

    while (window.isOpen()) {
        sf::Event event;


        while (window.pollEvent(event)) {
            ImGui::SFML::ProcessEvent(event);

            if (event.type == sf::Event::Closed)
                window.close();
        }
        ImGui::SFML::Update(window, sf::seconds(1.0f / 60.0f));

        if (showLoginForm) {
            if (!dataFetched) {
                clientInfo.clear();
                FetchClientInformation(username, password, clientInfo, operatorID);
                LoadTaskCompletedStatus();
                dataFetched = true;
            }

            ShowLoginForm(showLoginForm, username, password, loginError, clientInfo, operatorID);
        }
        else if (showTaskForm) {
            ShowTaskForm(currentTaskInfo, showLoginForm, showTaskForm, showCallForm, selectedTaskID, showCallCompletionForm);
        }
        else if (showCallForm) {
            isInCall = true;
            ShowCallForm(currentTaskInfo, showCallForm, showCallCompletionForm, callTimer, beginningTime, endTimeStr);
        }
        else if (showCallCompletionForm) {
            ShowCallCompletionForm(currentTaskInfo, showCallForm, showCallCompletionForm, showTaskForm, showInfoWindow, callTimer, beginningTime, endTimeStr);
        }
        else if (showInfoWindow) {
            ShowInfoWindow(currentTaskInfo, closeInfoWindow, showMainForm);
            if (closeInfoWindow) {
                showInfoWindow = false;
                closeInfoWindow = false;
            }
        }
        else if (showMainForm) {
            showTaskForm = false;
            showCallForm = false;
            showCallCompletionForm = false;
            showMainForm = false;
        }

        else {
            if (needToReloadData) {
                clientInfo.clear();
                FetchClientInformation(username, password, clientInfo, operatorID);
                LoadTaskCompletedStatus();
                needToReloadData = false;
            }
            ShowMainInterface(clientInfo, showLoginForm, showTaskForm, selectedTaskID, needToReloadData, username);
        }

        window.clear();
        ImGui::SFML::Render(window);
        window.display();
    }

    ImGui::SFML::Shutdown();
    CloseDatabaseConnection();

    return 0;
}

void ExecuteQuery(const std::string& query) {
    try {
        globalDb->exec(query);
    }
    catch (const SQLite::Exception& e) {
        std::cerr << "SQLite exception: " << e.what() << " (Code: " << e.getErrorCode() << ")" << std::endl;

        if (e.getErrorCode() == SQLITE_CONSTRAINT_UNIQUE) {
            std::cerr << "Error: Unique constraint violation. The data you're trying to insert might already exist." << std::endl;
        }

        std::cerr << "Error executing query: " << query << std::endl;

        throw;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        throw;
    }
    catch (...) {
        std::cerr << "An unknown exception occurred." << std::endl;
        throw;
    }
}

void ConnectToDatabase() {
    try {
        globalDb = new SQLite::Database(DB_FILE, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        std::cout << "Подключение к базе данных." << std::endl;

        ExecuteQuery("PRAGMA encoding = 'UTF-8';");
        if (!globalDb->tableExists("Users")) {
            std::cout << "Creating Users table..." << std::endl;
            ExecuteQuery("CREATE TABLE Users (ID INTEGER PRIMARY KEY, Username TEXT, Password TEXT);");
        }
    }
    catch (const SQLite::Exception& e) {
        std::cerr << "SQLite exception: " << e.what() << " (Code: " << e.getErrorCode() << ")" << std::endl;
        throw;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        throw;
    }
    catch (...) {
        std::cerr << "An unknown exception occurred." << std::endl;
        throw;
    }
}

void CloseDatabaseConnection() {
    delete globalDb;
}

void FetchClientInformation(const std::string& username, const std::string& password, std::vector<std::vector<std::string>>& result, int& operatorID) {
    try {
        // Получаем ID пользователя по логину и паролю
        const std::string userQuery = "SELECT ID, ID_Operator FROM Users WHERE Username = :username AND Password = :password;";
        std::cout << "Executing SQL query: " << userQuery << std::endl;
        SQLite::Statement userStmt(*globalDb, userQuery);
        userStmt.bind(":username", username);
        userStmt.bind(":password", password);

        if (userStmt.executeStep()) {
            const int userID = userStmt.getColumn("ID");
            operatorID = userStmt.getColumn("ID_Operator");
            std::cout << "UserID: " << userID << std::endl;
            std::cout << "OperatorID: " << operatorID << std::endl;

            // Получаем информацию о клиенте для всех пользователей
            const std::string clientInfoQuery = "SELECT Tasks_List.ID as TaskID, Tasks_List.Offer_Text, "
                "Abonents_List.Abonent_Surname, Abonents_List.Abonent_Name, Abonents_List.Abonent_Middle_Name, "
                "Abonents_List.Abonent_Contact_Number, Abonents_List.Profession, "
                "Education.Education_Level, Tasks_List.isTaskCompleted "
                "FROM Tasks_List "
                "JOIN Abonents_List ON Tasks_List.ID_Customers = Abonents_List.ID "
                "JOIN Education ON Abonents_List.Abonent_Education_ID = Education.ID "
                "JOIN Call_Operators_List ON Tasks_List.ID = Call_Operators_List.ID_Task "
                "JOIN Operators_List ON Call_Operators_List.ID_Operator = Operators_List.ID "
                "WHERE date('now') BETWEEN Tasks_List.Beginning_Date AND Tasks_List.Ending_Date "
                "AND Operators_List.ID = :operatorID";

            std::cout << "Executing clientInfoQuery: " << clientInfoQuery << std::endl;

            SQLite::Statement clientInfoStmt(*globalDb, clientInfoQuery);
            clientInfoStmt.bind(":operatorID", operatorID);

            while (clientInfoStmt.executeStep()) {
                std::vector<std::string> row;

                const int taskID = clientInfoStmt.getColumn("TaskID");
                std::cout << "TaskID: " << taskID << std::endl;
                row.push_back(std::to_string(taskID));

                // Выводим информацию о задаче
                const std::string offerText = clientInfoStmt.getColumn("Offer_Text").getText();
                std::cout << "Offer_Text: " << offerText << std::endl;
                row.push_back(offerText);

                // Выводим ФИО
                const std::string abonentSurname = clientInfoStmt.getColumn("Abonent_Surname").getText();
                const std::string abonentName = clientInfoStmt.getColumn("Abonent_Name").getText();
                const std::string abonentMiddleName = clientInfoStmt.getColumn("Abonent_Middle_Name").getText();
                std::string fio = abonentSurname + " " + abonentName + " " + abonentMiddleName;
                std::cout << "FIO: " << fio << std::endl;
                row.push_back(fio);

                // Выводим номер телефона
                const std::string phoneNumber = clientInfoStmt.getColumn("Abonent_Contact_Number").getText();
                std::cout << "Phone Number: " << phoneNumber << std::endl;
                row.push_back(phoneNumber);

                // Выводим профессию
                const std::string profession = clientInfoStmt.getColumn("Profession").getText();
                std::cout << "Profession: " << profession << std::endl;
                row.push_back(profession);

                // Выводим уровень образования
                const std::string educationLevel = clientInfoStmt.getColumn("Education_Level").getText();
                std::cout << "Education Level: " << educationLevel << std::endl;
                row.push_back(educationLevel);

                // Выводим статус выполнения задачи
                const int isTaskCompleted = clientInfoStmt.getColumn("isTaskCompleted");
                std::cout << "Task Completed: " << isTaskCompleted << std::endl;
                row.push_back(std::to_string(isTaskCompleted));

                result.push_back(row);
            }
        }
        else {
            std::cerr << "Invalid login." << std::endl;
        }
    }
    catch (const SQLite::Exception& e) {
        std::cerr << "SQLite exception in FetchClientInformation: " << e.what() << " (Code: " << e.getErrorCode() << ")" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in FetchClientInformation: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "An unknown exception occurred in FetchClientInformation." << std::endl;
    }
}

void ShowLoginForm(bool& showLoginForm, std::string& username, std::string& password, bool& loginError, std::vector<std::vector<std::string>>& clientInfo, int& operatorID) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - 800) * 0.5f, (io.DisplaySize.y - 600) * 0.5f));

    if (ImGui::Begin(u8"Авторизация", &showLoginForm, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text(u8"Уважаемый сотрудник!\nДля входа в систему введите логин и пароль");
        ImGui::SetWindowFontScale(1.5f);

        std::vector<char> usernameBuffer(32, L'\0');
        std::vector<char> passwordBuffer(32, L'\0');

        std::copy(username.begin(), username.end(), usernameBuffer.begin());
        std::copy(password.begin(), password.end(), passwordBuffer.begin());

        if (ImGui::InputText(u8"Логин", usernameBuffer.data(), usernameBuffer.size())) {
            username = usernameBuffer.data();
        }

        if (ImGui::InputText(u8"Пароль", passwordBuffer.data(), passwordBuffer.size(), ImGuiInputTextFlags_Password)) {
            password = passwordBuffer.data();
        }

        if (ImGui::Button(u8"Ок", ImVec2(120, 40))) {
            if (!username.empty() || !password.empty()) {
                FetchClientInformation(username, password, clientInfo, operatorID);
                if (!clientInfo.empty()) {
                    showLoginForm = false;
                    loginError = false;
                }
                else {
                    loginError = true;
                }
            }
        }

        if (loginError) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), u8"Неверно введены логин и пароль! Попробуйте еще раз.");
        }
    }
    ImGui::End();
}

std::string GetOperatorFullNameFromDatabase(int operatorID) {
    std::string operatorFullName;

    try {
        // Выполните SQL-запрос для получения имени оператора по ID
        const std::string query = "SELECT Operator_Surname, Operator_Name, Operator_Middle_Name FROM Operators_List WHERE ID = :OperatorID;";
        SQLite::Statement stmt(*globalDb, query);
        stmt.bind(":OperatorID", operatorID);

        if (stmt.executeStep()) {
            std::string surname = stmt.getColumn("Operator_Surname").getText();
            std::string name = stmt.getColumn("Operator_Name").getText();
            std::string middleName = stmt.getColumn("Operator_Middle_Name").getText();

            // Сформируйте полное имя
            operatorFullName = surname + " " + name + " " + middleName;
        }
    }
    catch (const SQLite::Exception& e) {
        // Обработка исключений
        std::cerr << "SQLite exception in GetOperatorFullNameFromDatabase: " << e.what() << " (Code: " << e.getErrorCode() << ")" << std::endl;
    }

    return operatorFullName;
}

std::string GetOperatorFullName(const std::string& username) {
    // Выполните SQL-запрос для получения ID оператора по username
    const std::string idQuery = "SELECT ID FROM Users WHERE Username = :Username;";
    SQLite::Statement idStmt(*globalDb, idQuery);
    idStmt.bind(":Username", username);

    int operatorID = -1;
    if (idStmt.executeStep()) {
        operatorID = idStmt.getColumn("ID").getInt();
    }

    // Получите полное имя оператора из базы данных
    std::string operatorFullName = GetOperatorFullNameFromDatabase(operatorID);

    return operatorFullName;
}

void MarkTaskAsCompleted(int taskID) {
    try {
        // Выполните SQL-запрос для обновления статуса выполнения задачи
        const std::string query = "UPDATE Tasks_List SET isTaskCompleted = 1 WHERE ID = :TaskID;";
        SQLite::Statement stmt(*globalDb, query);
        stmt.bind(":TaskID", taskID);
        stmt.exec();
        if (taskID > 0 && taskID <= taskCompletedStatus.size()) {
            size_t taskIndex = taskID - 1;
            taskCompletedStatus[taskIndex] = true;
        }
    }
    catch (const SQLite::Exception& e) {
        // Обработка исключений
        std::cerr << "SQLite exception in MarkTaskAsCompleted: " << e.what() << " (Code: " << e.getErrorCode() << ")" << std::endl;
    }
}

void LoadTaskCompletedStatus() {
    try {
        const std::string query = "SELECT ID, isTaskCompleted FROM Tasks_List;";
        SQLite::Statement stmt(*globalDb, query);

        taskCompletedStatus.clear();

        while (stmt.executeStep()) {
            int taskID = stmt.getColumn("ID").getInt();
            bool isCompleted = stmt.getColumn("isTaskCompleted").getInt() == 1;

            // Расширяем вектор, если это необходимо, чтобы вместить текущую задачу
            if (taskID > taskCompletedStatus.size()) {
                taskCompletedStatus.resize(taskID);
            }

            taskCompletedStatus[taskID - 1] = isCompleted;
        }
    }
    catch (const SQLite::Exception& e) {
        std::cerr << "SQLite exception during task status loading: " << e.what() << " (Code: " << e.getErrorCode() << ")" << std::endl;
    }
}

void ShowMainInterface(std::vector<std::vector<std::string>>& clientInfo, bool& showLoginForm, bool& showTaskForm, int& selectedTaskID, bool& needToReloadData, const std::string& username) {
    if (needToReloadData) {
        LoadTaskCompletedStatus();
        needToReloadData = false;
    }
    ImGui::SetNextWindowSize(ImVec2(1200, 800), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin(u8"Главная форма", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_MenuBar);
    ImGui::SetWindowFontScale(1.5f);

    // Кнопка выхода из программы
    if (ImGui::Button(u8"Выход из системы", ImVec2(150, 40))) {
        showLoginForm = true;
        showTaskForm = false;
        needToReloadData = true;
        clientInfo.clear();
    }
    ImGui::Dummy(ImVec2(0, 20));

    std::string operatorFullName = GetOperatorFullName(username);
    ImGui::Text(u8"Добро пожаловать, %s", operatorFullName.c_str());

    ImGui::Dummy(ImVec2(0, 20));

    // Отображение данных
    if (!clientInfo.empty()) {
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, 200), false, ImGuiWindowFlags_HorizontalScrollbar);

        // Создаем таблицу с 6 столбцами
        ImGui::Columns(6, "ClientInfoColumns", false);  // Увеличил количество столбцов на 1

        // Установка фиксированной ширины для каждого столбца
        ImGui::SetColumnWidth(0, 50);  // Галочка (выполнено)
        ImGui::SetColumnWidth(1, 200); // Задача
        ImGui::SetColumnWidth(2, 250); // ФИО абонента
        ImGui::SetColumnWidth(3, 150); // Номер телефона
        ImGui::SetColumnWidth(4, 150); // Профессия
        ImGui::SetColumnWidth(5, 150); // Образование

        // Заголовки столбцов
        ImGui::Text(""); // Пустой столбец для галочек
        ImGui::NextColumn();
        ImGui::Text(u8"Задача");
        ImGui::NextColumn();
        ImGui::Text(u8"ФИО абонента");
        ImGui::NextColumn();
        ImGui::Text(u8"Номер телефона");
        ImGui::NextColumn();
        ImGui::Text(u8"Профессия");
        ImGui::NextColumn();
        ImGui::Text(u8"Образование");
        ImGui::NextColumn();

        // Разделитель
        ImGui::Separator();

        // Отображение данных
        for (size_t rowIndex = 0; rowIndex < clientInfo.size(); ++rowIndex) {
            const auto& row = clientInfo[rowIndex];

            bool taskCompleted = taskCompletedStatus[rowIndex];

            ImGuiSelectableFlags flags = taskCompleted ? ImGuiSelectableFlags_Disabled : 0;

            const char* checkSymbol = taskCompletedStatus[rowIndex] ? u8"+" : u8"-";
            ImGui::Text("%s", checkSymbol);
            ImGui::NextColumn();

            // Создаем строку таблицы как кнопку
            if (ImGui::Selectable(row[1].c_str(), false, flags | ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 20))) {
                selectedTaskID = std::stoi(row[0]);
                showTaskForm = true;
                currentTaskInfo = row;
            }

            // Остальные столбцы
            ImGui::NextColumn();
            ImGui::Text(row[2].c_str());
            ImGui::NextColumn();
            ImGui::Text(row[3].c_str());
            ImGui::NextColumn();
            ImGui::Text(row[4].c_str());
            ImGui::NextColumn();
            ImGui::Text(row[5].c_str());
            ImGui::NextColumn();
        }

        ImGui::EndChild();
        ImGui::Columns(1);
    }
    else {
        ImGui::Text("No information available.");
    }

    ImGui::Columns(1);

    ImGui::End();
}

void ShowTaskForm(const std::vector<std::string>& taskInfo, bool& showLoginForm, bool& showTaskForm, bool& showCallForm, int& selectedTaskID, bool& showCallCompletionForm) {
    try {
        // Проверяем, выбрана ли задача
        if (!taskInfo.empty() && selectedTaskID != -1 && std::stoi(taskInfo[0]) == selectedTaskID) {
            // Получаем ФИО абонента из таблицы Abonents_List
            const std::string abonentInfoQuery = "SELECT Abonent_Surname, Abonent_Name, Abonent_Middle_Name FROM Abonents_List WHERE ID = :ID;";
            SQLite::Statement abonentInfoStmt(*globalDb, abonentInfoQuery);
            abonentInfoStmt.bind(":ID", taskInfo[0]); // Предполагаем, что taskInfo[0] содержит TaskID
            std::string abonentSurname, abonentName, abonentMiddleName;

            if (abonentInfoStmt.executeStep()) {
                abonentSurname = abonentInfoStmt.getColumn("Abonent_Surname").getText();
                abonentName = abonentInfoStmt.getColumn("Abonent_Name").getText();
                abonentMiddleName = abonentInfoStmt.getColumn("Abonent_Middle_Name").getText();
            }

            // Получаем интересы абонента из таблицы Abonents_Interests_List
            const std::string interestsQuery = "SELECT Interests_List.Interest_Name FROM Abonents_Interests_List "
                "JOIN Interests_List ON Abonents_Interests_List.ID_Interest = Interests_List.ID "
                "WHERE Abonents_Interests_List.ID_Abonent = :ID;";
            SQLite::Statement interestsStmt(*globalDb, interestsQuery);
            interestsStmt.bind(":ID", taskInfo[0]); // Предполагаем, что taskInfo[0] содержит TaskID
            std::vector<std::string> interests;

            while (interestsStmt.executeStep()) {
                interests.push_back(interestsStmt.getColumn("Interest_Name").getText());
            }

            // Получаем FAQ из базы данных
            const std::string faqQuery = "SELECT Questions, Answers FROM FAQ WHERE ID = :ID;";
            SQLite::Statement faqStmt(*globalDb, faqQuery);
            faqStmt.bind(":ID", taskInfo[0]); // Предполагаем, что taskInfo[0] содержит TaskID
            std::vector<std::pair<std::string, std::string>> faqEntries;

            while (faqStmt.executeStep()) {
                std::string question = faqStmt.getColumn("Questions").getText();
                std::string answer = faqStmt.getColumn("Answers").getText();
                faqEntries.emplace_back(question, answer);
            }

            // Получаем общий комментарий из базы данных
            const std::string commentQuery = "SELECT All_Comment FROM Connection_of_Task_with_operator_And_Abonent WHERE ID_Abonent = :ID;";
            SQLite::Statement commentStmt(*globalDb, commentQuery);
            commentStmt.bind(":ID", taskInfo[0]); // Предполагаем, что taskInfo[0] содержит TaskID
            std::string allComment = commentStmt.executeStep() ? commentStmt.getColumn("All_Comment").getText() : "";

            ImGui::SetNextWindowSize(ImVec2(1000, 600), ImGuiCond_FirstUseEver);  // Wider window
            ImGui::Begin(u8"Информация по задаче", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_MenuBar);
            ImGui::SetWindowFontScale(1.5f);

            // Кнопка выхода из программы
            if (ImGui::Button(u8"Выход из системы", ImVec2(150, 40))) {
                showLoginForm = true; // Показать форму входа при нажатии кнопки
            }

            ImGui::Separator();

            // Левая часть формы
            ImGui::Columns(2, "TaskDetailsColumns", false);

            // Левая часть формы
            ImGui::Text(u8"ФИО абонента:");
            ImGui::Text(u8"%s %s %s", abonentSurname.c_str(), abonentName.c_str(), abonentMiddleName.c_str());
            ImGui::Separator();

            ImGui::Text(u8"Интересы абонента:");
            for (const auto& interest : interests) {
                ImGui::BulletText(u8"%s", interest.c_str());
            }

            // Правая часть формы
            ImGui::NextColumn();

            // Вывод данных по задаче в виде таблицы
            ImGui::Text(u8"Данные по задаче");
            ImGui::Separator();

            // Вывод текста задачи
            ImGui::TextWrapped(u8"Текст задачи:\n%s", taskInfo[1].c_str());
            ImGui::Separator();

            ImGui::Text(u8"Общий комментарий:");
            ImGui::InputTextMultiline("##AllComment", &allComment[0], allComment.size(), ImVec2(-1, 80), ImGuiInputTextFlags_ReadOnly);

            ImGui::Dummy(ImVec2(0, 20));

            ImGui::Text(u8"FAQ:");
            ImGui::Separator();

            // Выводим FAQ в виде таблицы
            ImGui::Columns(2, "FAQColumns", false);
            ImGui::SetColumnWidth(0, 300);
            ImGui::SetColumnWidth(1, 600);

            for (const auto& faqEntry : faqEntries) {
                ImGui::TextWrapped(u8"%s: %s", faqEntry.first.c_str(), faqEntry.second.c_str());
                ImGui::Separator();
            }

            ImGui::Columns(1);

            // Поместим кнопку "Позвонить" под таблицей
            ImGui::Dummy(ImVec2(0, 20));
            if (ImGui::Button(u8"Позвонить", ImVec2(-1, 40))) {
                showCallForm = true;
                showCallCompletionForm = false;
                showTaskForm = false;
            }
        }
        else {
            // Если задача не выбрана, сбрасываем флаги для других форм
            showCallForm = false;
            showCallCompletionForm = false;
            showTaskForm = false;
        }
        ImGui::Dummy(ImVec2(0, 20));

        // Добавляем кнопку для перехода на главную форму
        if (ImGui::Button(u8"Вернуться к задачам", ImVec2(-1, 40))) {
            showTaskForm = false;
        }
        ImGui::End();
    }
    catch (const SQLite::Exception& e) {
        std::cerr << "SQLite exception in ShowTaskForm: " << e.what() << " (Code: " << e.getErrorCode() << ")" << std::endl;
        std::cerr << "Query: " << e.getErrorCode() << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in ShowTaskForm: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "An unknown exception occurred in ShowTaskForm." << std::endl;
    }
}

void ShowCallForm(const std::vector<std::string>& taskInfo, bool& showCallForm, bool& showCallCompletionForm, int& callTimer, std::string& beginningTime, std::string& endTimeStr) {
    std::vector<std::string> eventVariants;
    std::vector<std::pair<std::string, std::string>> faqForCallEntries;
    std::vector<std::string> interestsForCall;

    try {
        // Выполним запрос к таблице Variants_Of_Events
        const std::string eventsQuery = "SELECT Variant_Of_Events FROM Variants_Of_Results;";
        SQLite::Statement eventsStmt(*globalDb, eventsQuery);

        while (eventsStmt.executeStep()) {
            eventVariants.push_back(eventsStmt.getColumn("Variant_Of_Events").getText());
        }

        // Получаем FAQ для звонка из базы данных
        const std::string faqQueryForCall = "SELECT Questions, Answers FROM FAQ WHERE ID = :ID;";
        SQLite::Statement faqForCallStmt(*globalDb, faqQueryForCall);
        faqForCallStmt.bind(":ID", taskInfo[0]);

        while (faqForCallStmt.executeStep()) {
            std::string question = faqForCallStmt.getColumn("Questions").getText();
            std::string answer = faqForCallStmt.getColumn("Answers").getText();
            faqForCallEntries.emplace_back(question, answer);
        }

        // Получаем интересы абонента для звонка из базы данных
        const std::string interestsQueryForCall = "SELECT Interests_List.Interest_Name FROM Abonents_Interests_List "
            "JOIN Interests_List ON Abonents_Interests_List.ID_Interest = Interests_List.ID "
            "WHERE Abonents_Interests_List.ID_Abonent = :ID;";
        SQLite::Statement interestsForCallStmt(*globalDb, interestsQueryForCall);
        interestsForCallStmt.bind(":ID", taskInfo[0]);

        while (interestsForCallStmt.executeStep()) {
            interestsForCall.push_back(interestsForCallStmt.getColumn("Interest_Name").getText());
        }
    }
    catch (const SQLite::Exception& e) {
        std::cerr << "SQLite exception in ShowCallForm - fetching event variants: " << e.what() << " (Code: " << e.getErrorCode() << ")" << std::endl;
    }

    try {
        std::time_t t = std::time(nullptr);
        std::tm* now = std::localtime(&t);

        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(u8"Форма звонка", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_MenuBar)) {
            ImGui::SetWindowFontScale(1.5f);

            ImGui::Text(u8"Текст задачи:");
            ImGui::TextWrapped("%s", taskInfo[1].c_str());
            ImGui::Separator();

            // FAQ для звонка:
            ImGui::Text(u8"FAQ:");
            ImGui::Separator();

            // Выводим FAQ для звонка в виде таблицы
            ImGui::Columns(2, "FAQForCallColumns", false);
            ImGui::SetColumnWidth(0, 300);
            ImGui::SetColumnWidth(1, 600);

            for (const auto& faqForCallEntry : faqForCallEntries) {
                ImGui::TextWrapped(u8"%s: %s", faqForCallEntry.first.c_str(), faqForCallEntry.second.c_str());
                ImGui::Separator();
            }

            ImGui::Columns(1);

            // Интересы абонента для звонка:
            ImGui::Text(u8"Интересы абонента для звонка:");
            for (const auto& interestForCall : interestsForCall) {
                ImGui::BulletText(u8"%s", interestForCall.c_str());
            }

            ImGui::Dummy(ImVec2(0, 20));

            if (!ImGui::IsMouseDown(0)) {
                // Обновляем таймер, если левая кнопка мыши не нажата
                callTimer += isInCall ? 1 : 0;
            }
            ImGui::Text(u8"Идёт звонок: %02d:%02d", callTimer / 60, callTimer % 60);

            ImGui::Dummy(ImVec2(0, 20));

            // Получение времени начала звонка
            std::stringstream beginningTimeStream;
            beginningTimeStream << std::put_time(now, "%Y-%m-%d %H:%M:%S");
            beginningTime = beginningTimeStream.str();

            // Получение времени завершения звонка
            std::time_t endTime = std::mktime(now) + callTimer;
            std::stringstream endTimeStream;
            endTimeStream << std::put_time(std::localtime(&endTime), "%Y-%m-%d %H:%M:%S");
            endTimeStr = endTimeStream.str();

            // Получим ID текущего оператора из вашей базы данных
            const std::string operatorIDQuery = "SELECT ID FROM Operators_List WHERE Operator_Name = :OperatorName;";
            SQLite::Statement operatorIDStmt(*globalDb, operatorIDQuery);
            operatorIDStmt.bind(":OperatorName", std::stoi(taskInfo[0]));

            int operatorID = operatorIDStmt.executeStep() ? operatorIDStmt.getColumn("ID").getInt() : 1;

            // Получим ID абонента, которому звонили, из ваших данных
            int abonentID = std::stoi(taskInfo[0]); // Предполагаем, что taskInfo[0] содержит ID абонента

            if (ImGui::Button(u8"Завершить звонок", ImVec2(-1, 40))) {
                isInCall = false;
                showCallForm = false;
                showCallCompletionForm = true;
            }

            ImGui::End();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in ShowCallForm: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "An unknown exception occurred in ShowCallForm." << std::endl;
    }
}

void ShowCallCompletionForm(const std::vector<std::string>& taskInfo, bool& showCallForm, bool& showCallCompletionForm, bool& showTaskForm, bool& showInfoWindow, int& callTimer, std::string& beginningTime, std::string& endTimeStr) {
    std::vector<std::string> eventVariants;
    std::vector<std::pair<std::string, std::string>> faqForCallEntries;
    std::vector<std::string> interestsForCall;

    try {
        // Выполним запрос к таблице Variants_Of_Events
        const std::string eventsQuery = "SELECT Variant_Of_Events FROM Variants_Of_Results;";
        SQLite::Statement eventsStmt(*globalDb, eventsQuery);

        while (eventsStmt.executeStep()) {
            eventVariants.push_back(eventsStmt.getColumn("Variant_Of_Events").getText());
        }

        // Получаем FAQ для звонка из базы данных
        const std::string faqQueryForCall = "SELECT Questions, Answers FROM FAQ WHERE ID = :ID;";
        SQLite::Statement faqForCallStmt(*globalDb, faqQueryForCall);
        faqForCallStmt.bind(":ID", taskInfo[0]);

        while (faqForCallStmt.executeStep()) {
            std::string question = faqForCallStmt.getColumn("Questions").getText();
            std::string answer = faqForCallStmt.getColumn("Answers").getText();
            faqForCallEntries.emplace_back(question, answer);
        }

        // Получаем интересы абонента для звонка из базы данных
        const std::string interestsQueryForCall = "SELECT Interests_List.Interest_Name FROM Abonents_Interests_List "
            "JOIN Interests_List ON Abonents_Interests_List.ID_Interest = Interests_List.ID "
            "WHERE Abonents_Interests_List.ID_Abonent = :ID;";
        SQLite::Statement interestsForCallStmt(*globalDb, interestsQueryForCall);
        interestsForCallStmt.bind(":ID", taskInfo[0]);

        while (interestsForCallStmt.executeStep()) {
            interestsForCall.push_back(interestsForCallStmt.getColumn("Interest_Name").getText());
        }
    }
    catch (const SQLite::Exception& e) {
        std::cerr << "SQLite exception in ShowCallCompletionForm - fetching event variants: " << e.what() << " (Code: " << e.getErrorCode() << ")" << std::endl;
    }

    try {
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(u8"Форма завершения звонка", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_MenuBar)) {
            ImGui::SetWindowFontScale(1.5f);

            // Добавим поле для комментария
            static char commentBuffer[256] = "";
            ImGui::InputTextMultiline(u8"Комментарий", commentBuffer, IM_ARRAYSIZE(commentBuffer), ImVec2(-1, 80));

            // Добавим выпадающий список для выбора результата
            static int selectedResult = 0;
            std::vector<const char*> resultVariants;

            if (eventVariants.empty()) {
                resultVariants.push_back(u8"Выберите результат");
            }
            else {
                for (size_t i = 0; i < eventVariants.size(); ++i) {
                    resultVariants.push_back(eventVariants[i].c_str());
                }
            }

            ImGui::Dummy(ImVec2(0, 30));
            ImGui::Combo(u8"Выберите результат", &selectedResult, resultVariants.data(), resultVariants.size());
            ImGui::Dummy(ImVec2(0, 30));

            // Получим ID текущего оператора из вашей базы данных
            const std::string operatorIDQuery = "SELECT ID FROM Operators_List WHERE Operator_Name = :OperatorName;";
            SQLite::Statement operatorIDStmt(*globalDb, operatorIDQuery);
            operatorIDStmt.bind(":OperatorName", std::stoi(taskInfo[0]));

            int operatorID = operatorIDStmt.executeStep() ? operatorIDStmt.getColumn("ID").getInt() : 1;

            // Получим ID абонента, которому звонили, из ваших данных
            int abonentID = std::stoi(taskInfo[0]);

            if (ImGui::Button(u8"К задачам", ImVec2(-1, 40))) {
                showTaskForm = false;
                showCallForm = false;
                showCallCompletionForm = false;
                showInfoWindow = true;
                SQLite::Transaction transaction(*globalDb);

                try {
                    size_t taskID = std::stoi(taskInfo[0]);
                    size_t vectorIndex = taskID - 1;  // Индекс вектора (учтем, что векторы индексируются с 0)

                    // Проверим, что taskID соответствует вектору taskCompletedStatus
                    if (vectorIndex >= 0 && vectorIndex < taskCompletedStatus.size()) {
                        // Здесь производится запись в базу данных для завершенного звонка
                        const std::string insertQuery = "INSERT INTO Call_Result (Call_Lasting, Beginning_Time_Of_Call, Ending_Time_Of_Call, "
                            "One_Comment, ID_Operator, ID_Task, ID_Result, ID_Abonent) VALUES (:CallLasting, :BeginningTime, :EndTime, "
                            ":Comment, :OperatorID, :TaskID, :ResultID, :AbonentID);";

                        SQLite::Statement insertStmt(*globalDb, insertQuery);

                        // Биндим параметры запроса
                        insertStmt.bind(":CallLasting", callTimer);
                        insertStmt.bind(":BeginningTime", beginningTime);
                        insertStmt.bind(":EndTime", endTimeStr);
                        insertStmt.bind(":Comment", commentBuffer);
                        insertStmt.bind(":OperatorID", operatorID);
                        insertStmt.bind(":TaskID", std::stoi(taskInfo[0]));
                        insertStmt.bind(":ResultID", selectedResult);
                        insertStmt.bind(":AbonentID", abonentID);

                        // Выполняем запрос
                        insertStmt.exec();
                        transaction.commit();

                        // Помечаем задачу как выполненную после успешной записи в базу данных
                        taskCompletedStatus[vectorIndex] = true;

                        // Обновляем статус задачи в базе данных
                        MarkTaskAsCompleted(std::stoi(taskInfo[0]));
                    }
                    else {
                        // Обработка случая, когда taskID не соответствует вектору taskCompletedStatus
                        std::cerr << "Invalid task ID: " << taskID << std::endl;
                    }
                }
                catch (const SQLite::Exception& e) {
                    std::cerr << "SQLite exception during call completion: " << e.what() << " (Code: " << e.getErrorCode() << ")" << std::endl;
                    transaction.rollback();
                }
            }
            ImGui::End();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Exception in ShowCallCompletionForm: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "An unknown exception occurred in ShowCallCompletionForm." << std::endl;
    }
}

void ShowInfoWindow(const std::vector<std::string>& taskInfo, bool& closeInfoWindow, bool& showMainForm) {
    ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(u8"Записанная информация", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_MenuBar)) {
        ImGui::SetWindowFontScale(1.5f);

        // Отображаем информацию о задаче из taskInfo
        ImGui::Text(u8"ID задачи: %s", taskInfo[0].c_str());
        ImGui::Text(u8"Название задачи: %s", taskInfo[1].c_str());
        ImGui::Text(u8"Описание задачи: %s", taskInfo[2].c_str());

        // Добавляем кнопку для перехода в главную форму
        if (ImGui::Button(u8"Ок", ImVec2(-1, 40))) {
            closeInfoWindow = true;
        }

        ImGui::End();
    }
}