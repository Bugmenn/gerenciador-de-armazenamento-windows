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

# Lista de exclusoes do usuario (PathExclusion.h): impede que uma pasta
# protegida, ou uma pasta-pai dela, seja oferecida para limpeza.
add_executable(test_path_exclusion
    test_path_exclusion.cpp
    ${CMAKE_SOURCE_DIR}/src/PathExclusion.cpp
)
target_include_directories(test_path_exclusion PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME path_exclusion COMMAND test_path_exclusion)

# Progresso geral por etapas (ProgressState.h, header-only): o anel de
# progresso da UI depende de OverallFraction e de o canal preservar a etapa.
add_executable(test_progress test_progress.cpp)
target_include_directories(test_progress PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME progress COMMAND test_progress)

# Agrupamento/filtro/ordenacao da aba Resultados e bytes limpaveis por
# unidade (ItemGrouping.h): a lista agrupada e a rosca das unidades dependem disso.
add_executable(test_item_grouping
    test_item_grouping.cpp
    ${CMAKE_SOURCE_DIR}/src/ItemGrouping.cpp
    ${CMAKE_SOURCE_DIR}/src/PathExclusion.cpp
)
target_include_directories(test_item_grouping PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME item_grouping COMMAND test_item_grouping)

# Classificacao dos dados orfaos em chance Baixa/Media/Alta e quais entram
# na limpeza automatica (OrphanClassifier.h, ScanItem.h).
add_executable(test_orphan_classifier
    test_orphan_classifier.cpp
    ${CMAKE_SOURCE_DIR}/src/OrphanClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/PathExclusion.cpp
)
target_include_directories(test_orphan_classifier PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME orphan_classifier COMMAND test_orphan_classifier)

# Mesma leitura de fonte do app (ver /utf-8 no CMakeLists.txt raiz): os headers
# incluidos tem literais acentuados.
if(MSVC)
    target_compile_options(test_duplicate_grouping PRIVATE /utf-8)
    target_compile_options(test_category_keys PRIVATE /utf-8)
    target_compile_options(test_path_exclusion PRIVATE /utf-8)
    target_compile_options(test_progress PRIVATE /utf-8)
    # /EHsc: ItemGrouping.h inclui ScanResult.h (<chrono>), que exige
    # semantica de excecao (C4530 sem a flag).
    target_compile_options(test_item_grouping PRIVATE /utf-8 /EHsc)
    target_compile_options(test_orphan_classifier PRIVATE /utf-8 /EHsc)
endif()
