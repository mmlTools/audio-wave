[CmdletBinding()] 
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',

    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Build-Windows.ps1 requires CI environment"
}

if ( -not [System.Environment]::Is64BitOperatingSystem ) {
    throw "A 64-bit system is required to build the project."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The obs-studio PowerShell build script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Build {
    trap {
        Pop-Location -Stack BuildTemp -ErrorAction 'SilentlyContinue'
        Write-Error $_
        Log-Group
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."

    # Load helper functions (Log-Group, Ensure-Location, Invoke-External, etc.)
    $UtilityFunctions = Get-ChildItem -Path "$ScriptHome/utils.pwsh/*.ps1" -Recurse
    foreach ($Utility in $UtilityFunctions) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    # Read buildspec for name/version
    $BuildSpecFile = Join-Path $ProjectRoot "buildspec.json"
    if (-not (Test-Path $BuildSpecFile)) {
        throw "Buildspec not found at ${BuildSpecFile}"
    }

    $BuildSpec = Get-Content -Path $BuildSpecFile -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    if (-not $ProductName -or -not $ProductVersion) {
        throw "buildspec.json must contain 'name' and 'version'."
    }

    Push-Location -Stack BuildTemp
    Ensure-Location $ProjectRoot

    $CmakeArgs = @('--preset', "windows-ci-${Target}")
    $CmakeBuildArgs = @('--build')
    $CmakeInstallArgs = @()

    if ( $DebugPreference -eq 'Continue' ) {
        $CmakeArgs += '--debug-output'
        $CmakeBuildArgs += '--verbose'
        $CmakeInstallArgs += '--verbose'
    }

    $CmakeBuildArgs += @(
        '--preset', "windows-${Target}",
        '--config', $Configuration,
        '--parallel',
        '--', '/consoleLoggerParameters:Summary', '/noLogo'
    )

    $InstallPrefix = "${ProjectRoot}/release/${Configuration}"

    $CmakeInstallArgs += @(
        '--install', "build_${Target}",
        '--prefix', $InstallPrefix,
        '--config', $Configuration
    )

    Log-Group "Configuring ${ProductName}..."
    Invoke-External cmake @CmakeArgs

    Log-Group "Building ${ProductName}..."
    Invoke-External cmake @CmakeBuildArgs

    Log-Group "Installing ${ProductName}..."
    Invoke-External cmake @CmakeInstallArgs

    Log-Group

    Pop-Location -Stack BuildTemp

    # ------------------------------------------------------------------
    # Build the Windows installer (.exe) with NSIS
    # ------------------------------------------------------------------
    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"

    $InstallerScript = Join-Path $ProjectRoot "installer\audio-wave-installer.nsi"
    if (-not (Test-Path $InstallerScript)) {
        Write-Warning "NSIS script not found at: ${InstallerScript} – skipping installer build."
        return
    }

    $InstallerOutput = "${ProjectRoot}/release/${OutputName}-Setup.exe"

    # Ensure makensis is available
    $MakensisCmd = Get-Command "makensis" -ErrorAction SilentlyContinue
    if (-not $MakensisCmd) {
        Write-Warning "makensis (NSIS) was not found in PATH – skipping installer build."
        return
    }

    Log-Group "Building ${ProductName} installer..."

    $MakensisArgs = @(
        "/DPRODUCT_NAME=$ProductName",
        "/DPRODUCT_VERSION=$ProductVersion",
        "/DPROJECT_ROOT=$ProjectRoot",
        "/DCONFIGURATION=$Configuration",
        "/DTARGET=$Target",
        "/DOUTPUT_EXE=$InstallerOutput",
        $InstallerScript
    )

    & $MakensisCmd.Source @MakensisArgs
    if ($LASTEXITCODE -ne 0) {
        throw "NSIS makensis failed with exit code $LASTEXITCODE"
    }

    Log-Group

    Write-Host "Build complete:"
    Write-Host "  - Installed to: $InstallPrefix"
    Write-Host "  - Installer:    $InstallerOutput"
}

Build
