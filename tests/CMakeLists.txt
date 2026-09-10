# Só a lógica de agrupamento de duplicados é livre de APIs do Windows (ver
# storagecleaner/DuplicateGrouping.h) — o resto do projeto (scanners, config,
# histórico) depende de Win32/COM/VSS e só pode ser buildado numa máquina
# Windows real. Este teste roda de verdade neste ambiente de desenvolvimento.
add_executable(test_duplicate_grouping
    test_duplicate_grouping.cpp
    ${CMAKE_SOURCE_DIR}/src/DuplicateGrouping.cpp
)
target_include_directories(test_duplicate_grouping PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME duplicate_grouping COMMAND test_duplicate_grouping)
