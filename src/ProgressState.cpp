#include "storagecleaner/ProgressState.h"

namespace storagecleaner {

const char* PhaseLabel(Phase phase) {
    switch (phase) {
        case Phase::Idle:                   return "Ocioso";
        case Phase::ScanningTemp:            return "Escaneando arquivos temporarios...";
        case Phase::ScanningBrowserCache:    return "Escaneando cache de navegador...";
        case Phase::ScanningDuplicates:      return "Procurando arquivos duplicados...";
        case Phase::ScanningLogs:            return "Escaneando logs antigos...";
        case Phase::ScanningRecycleBin:      return "Verificando a Lixeira...";
        case Phase::ScanningRestorePoints:   return "Verificando pontos de restauracao...";
        case Phase::ScanningOrphanedApps:    return "Procurando dados de apps desinstalados...";
        case Phase::Cleaning:                return "Limpando itens selecionados...";
        case Phase::Done:                    return "Concluido";
        case Phase::Cancelled:               return "Cancelado";
        case Phase::Error:                   return "Erro";
    }
    return "Desconhecido";
}

} // namespace storagecleaner
