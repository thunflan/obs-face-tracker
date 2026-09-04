param (
    [string]$ObsVersion = "30",
    [string]$ReleaseDir = "release",
    [string]$PackageDir = "package"
)

$ErrorActionPreference = "Stop"

Write-Host "====================================================" -ForegroundColor Cyan
Write-Host "  Empacotamento dos 3 Plugins OBS Independentes" -ForegroundColor Cyan
Write-Host "====================================================" -ForegroundColor Cyan

# 1. Determina versão do pacote via git
$Version = "v0.9.1"
try {
    $gitVer = (git describe --tags --always 2>$null).Trim()
    if ($gitVer) { $Version = $gitVer }
} catch {}

Write-Host "Versão detectada: $Version" -ForegroundColor Yellow

# 2. Prepara diretório de pacotes
if (-not (Test-Path $PackageDir)) {
    New-Item -ItemType Directory -Path $PackageDir -Force | Out-Null
}

$TempDir = Join-Path $PackageDir "temp_staging"
if (Test-Path $TempDir) {
    Remove-Item $TempDir -Recurse -Force
}
New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

function Create-PluginZip {
    param (
        [string]$PluginName,
        [string[]]$PluginBinaries,
        [string]$DataSubdir
    )

    Write-Host "`n--> Criando pacote para: $PluginName..." -ForegroundColor Green
    $StageDir = Join-Path $TempDir $PluginName
    $StageBinDir = Join-Path $StageDir "obs-plugins\64bit"
    $StageDataDir = Join-Path $StageDir "data\obs-plugins\$PluginName"

    New-Item -ItemType Directory -Path $StageBinDir -Force | Out-Null
    New-Item -ItemType Directory -Path $StageDataDir -Force | Out-Null

    # Copia arquivos binários (DLLs / PDBs)
    foreach ($bin in $PluginBinaries) {
        $srcPath = Join-Path $ReleaseDir "obs-plugins\64bit\$bin"
        if (Test-Path $srcPath) {
            Copy-Item $srcPath -Destination $StageBinDir -Force
            Write-Host "  [+] Binario: $bin" -ForegroundColor Gray
        } else {
            # Tenta buscar em deps (ex: SDL2.dll)
            $altSrc = "deps\SDL2\lib\x64\$bin"
            if (Test-Path $altSrc) {
                Copy-Item $altSrc -Destination $StageBinDir -Force
                Write-Host "  [+] Binario (deps): $bin" -ForegroundColor Gray
            } else {
                Write-Host "  [!] Aviso: Binario $bin nao encontrado em $srcPath" -ForegroundColor Yellow
            }
        }
    }

    # Copia pasta de dados (locale / configs)
    $srcData = Join-Path $ReleaseDir "data\obs-plugins\$DataSubdir"
    if (Test-Path $srcData) {
        Copy-Item "$srcData\*" -Destination $StageDataDir -Recurse -Force
        Write-Host "  [+] Dados: $srcData -> $StageDataDir" -ForegroundColor Gray
    } else {
        # Fallback para pasta data/locale local do projeto
        if (Test-Path "data\locale") {
            $stageLocale = Join-Path $StageDataDir "locale"
            New-Item -ItemType Directory -Path $stageLocale -Force | Out-Null
            Copy-Item "data\locale\*" -Destination $stageLocale -Recurse -Force
            Write-Host "  [+] Locale local copiado para: $stageLocale" -ForegroundColor Gray
        }
    }

    # Copia arquivo de Licença
    if (Test-Path "LICENSE") {
        Copy-Item "LICENSE" -Destination (Join-Path $StageDataDir "LICENSE-$PluginName.txt") -Force
    }

    # Cria arquivo de instruções de instalação dentro do pacote
    $readmeContent = @"
========================================================================
  $PluginName - Guia de Instalacao no OBS Studio
========================================================================

COMO INSTALAR NESTE OU EM OUTRO COMPUTADOR:
1. Feche o OBS Studio se estiver aberto.
2. Copie as pastas 'obs-plugins' e 'data' contidas neste pacote para o
   diretorio onde o OBS Studio esta instalado.
   (Padrao: C:\Program Files\obs-studio\ )
3. Abra o OBS Studio.

DEPENDENCIAS E REQUISITOS:
- obs-face-tracker: Independente.
- obs-ptz-tracker: Independente.
- obs-joystick-controller: Necessita do plugin 'obs-ptz-tracker' instalado
  para acionar o movimento de Pan/Tilt/Zoom e os Presets de camera pelo joystick.
  (As trocas de cena e mesa de corte funcionam autonomamente).
"@
    Set-Content -Path (Join-Path $StageDir "LEIA-ME_INSTALACAO.txt") -Value $readmeContent -Encoding UTF8

    # Gera o arquivo ZIP versionado
    $ZipName = "$PluginName-$Version-obs$ObsVersion-Windows-x64.zip"
    $ZipPath = Join-Path $PackageDir $ZipName
    if (Test-Path $ZipPath) {
        Remove-Item $ZipPath -Force
    }

    Write-Host "  Compactando em: $ZipName..." -ForegroundColor Cyan
    Compress-Archive -Path "$StageDir\*" -DestinationPath $ZipPath -Force
    Write-Host "  [OK] Gerado com sucesso: $ZipPath" -ForegroundColor Green

    # Cria também uma cópia com nome fixo e direto
    $FriendlyZip = Join-Path $PackageDir "$PluginName.zip"
    Copy-Item $ZipPath -Destination $FriendlyZip -Force
    Write-Host "  [+] Copia amigavel: $FriendlyZip" -ForegroundColor Gray

    # Mostra o Hash SHA256 do pacote
    $Hash = Get-FileHash -Path $ZipPath -Algorithm SHA256
    Write-Host "  SHA256: $($Hash.Hash)" -ForegroundColor DarkGray
}

# ----------------------------------------------------
# 1. Plugin: obs-face-tracker
# ----------------------------------------------------
Create-PluginZip `
    -PluginName "obs-face-tracker" `
    -PluginBinaries @("obs-face-tracker.dll", "obs-face-tracker.pdb") `
    -DataSubdir "obs-face-tracker"

# ----------------------------------------------------
# 2. Plugin: obs-ptz-tracker
# ----------------------------------------------------
Create-PluginZip `
    -PluginName "obs-ptz-tracker" `
    -PluginBinaries @("obs-ptz-tracker.dll", "obs-ptz-tracker.pdb") `
    -DataSubdir "obs-ptz-tracker"

# ----------------------------------------------------
# 3. Plugin: obs-joystick-controller (Requer plugin PTZ)
# ----------------------------------------------------
Create-PluginZip `
    -PluginName "obs-joystick-controller" `
    -PluginBinaries @("obs-joystick-controller.dll", "obs-joystick-controller.pdb", "SDL2.dll") `
    -DataSubdir "obs-joystick-controller"

# Limpa diretório de staging temporário
Remove-Item $TempDir -Recurse -Force

Write-Host "`n====================================================" -ForegroundColor Cyan
Write-Host "  Todos os 3 ZIPs foram empacotados com sucesso!" -ForegroundColor Green
Write-Host "  Localizacao: $(Resolve-Path $PackageDir)" -ForegroundColor Green
Write-Host "====================================================" -ForegroundColor Cyan
