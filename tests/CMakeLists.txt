# Só a lógica de agrupamento de duplicados (storagecleaner/DuplicateGrouping.h)
# e as funções de categoria (storagecleaner/ScanItem.h) são livres de APIs do
# Windows — o resto do projeto (scanners, config, histórico) depende de
# Win32/COM/VSS e só pode ser buildado numa máquina Windows real.
add_executable(test_duplicate_grouping
    test_duplicate_grouping.cpp
    ${CMAKE_SOURCE_DIR}/src/DuplicateGrouping.cpp
)
target_include_directories(test_duplicate_grouping PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME duplicate_grouping COMMAND test_duplicate_grouping)

# Chaves de persistencia das categorias (ScanItem.h, header-only e sem Win32):
# fixa o formato de config.json/history.json ja gravados pelo usuario.
add_executable(test_category_keys test_category_keys.cpp)
target_include_directories(test_category_keys PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME category_keys COMMAND test_category_keys)

# Mesma leitura de fonte do app (ver /utf-8 no CMakeLists.txt raiz): os headers
# incluidos tem literais acentuados.
if(MSVC)
    target_compile_options(test_duplicate_grouping PRIVATE /utf-8)
    target_compile_options(test_category_keys PRIVATE /utf-8)
endif()
