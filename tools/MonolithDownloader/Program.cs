using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace LightOn.MonolithDownloader
{
    internal static class Program
    {
        [STAThread]
        private static int Main(string[] args)
        {
            string workspaceRoot = WorkspaceLocator.Find();

            if (args.Any(delegate(string value)
            {
                return string.Equals(value, "--headless", StringComparison.OrdinalIgnoreCase);
            }))
            {
                return RunHeadless(args, workspaceRoot);
            }

            bool createdNew;
            using (Mutex mutex = new Mutex(true, @"Local\LIGHT_ON_MonolithDownloader", out createdNew))
            {
                if (!createdNew)
                {
                    MessageBox.Show("LIGHT_ON 다운로더가 이미 실행 중입니다.", "LIGHT_ON",
                        MessageBoxButtons.OK, MessageBoxIcon.Information);
                    return 0;
                }

                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                Application.Run(new DownloadForm(workspaceRoot));
                GC.KeepAlive(mutex);
                return 0;
            }
        }

        private static int RunHeadless(string[] args, string workspaceRoot)
        {
            DateTime date = DateTime.Today;
            string output = Path.Combine(workspaceRoot, "exports");
            string sdPath = string.Empty;
            bool copySd = true;

            for (int i = 0; i < args.Length; i++)
            {
                if (string.Equals(args[i], "--date", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
                {
                    DateTime parsed;
                    if (!DateTime.TryParseExact(args[++i], "yyyy-MM-dd", CultureInfo.InvariantCulture,
                        DateTimeStyles.None, out parsed))
                    {
                        return 64;
                    }
                    date = parsed.Date;
                }
                else if (string.Equals(args[i], "--output", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
                {
                    output = args[++i];
                }
                else if (string.Equals(args[i], "--sd", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
                {
                    sdPath = args[++i];
                }
                else if (string.Equals(args[i], "--no-sd", StringComparison.OrdinalIgnoreCase))
                {
                    copySd = false;
                }
            }

            try
            {
                DownloadEngine engine = new DownloadEngine(workspaceRoot, delegate(string ignored) { });
                DownloadResult result = engine.Run(date, output, copySd, sdPath);
                return result.CsvSucceeded ? 0 : 2;
            }
            catch
            {
                return 1;
            }
        }
    }

    internal sealed class DownloadForm : Form
    {
        private readonly string workspaceRoot;
        private readonly DateTimePicker datePicker;
        private readonly TextBox outputTextBox;
        private readonly CheckBox copySdCheckBox;
        private readonly TextBox sdPathTextBox;
        private readonly Button sdBrowseButton;
        private readonly Button downloadButton;
        private readonly ProgressBar progressBar;
        private readonly Label statusLabel;
        private readonly TextBox logTextBox;
        private bool running;

        public DownloadForm(string workspaceRoot)
        {
            this.workspaceRoot = workspaceRoot;

            Text = "LIGHT_ON Monolith 데이터 다운로더";
            StartPosition = FormStartPosition.CenterScreen;
            MinimumSize = new Size(780, 570);
            Size = new Size(860, 650);
            Font = new Font("맑은 고딕", 9F);
            AutoScaleMode = AutoScaleMode.Dpi;

            TableLayoutPanel root = new TableLayoutPanel();
            root.Dock = DockStyle.Fill;
            root.Padding = new Padding(16);
            root.ColumnCount = 3;
            root.RowCount = 10;
            root.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 125F));
            root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            root.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 90F));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 46F));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 39F));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 39F));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 36F));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 39F));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 64F));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 46F));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 25F));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 30F));
            Controls.Add(root);

            Label title = new Label();
            title.Text = "오늘의 서버 CSV와 SD 카드 .log를 한 번에 내려받습니다.";
            title.Font = new Font(Font.FontFamily, 13F, FontStyle.Bold);
            title.Dock = DockStyle.Fill;
            title.TextAlign = ContentAlignment.MiddleLeft;
            root.Controls.Add(title, 0, 0);
            root.SetColumnSpan(title, 3);

            root.Controls.Add(CreateLabel("조회 날짜"), 0, 1);
            datePicker = new DateTimePicker();
            datePicker.Format = DateTimePickerFormat.Custom;
            datePicker.CustomFormat = "yyyy-MM-dd";
            datePicker.Value = DateTime.Today;
            datePicker.Dock = DockStyle.Fill;
            root.Controls.Add(datePicker, 1, 1);
            root.SetColumnSpan(datePicker, 2);

            root.Controls.Add(CreateLabel("저장 폴더"), 0, 2);
            outputTextBox = new TextBox();
            outputTextBox.Text = Path.Combine(workspaceRoot, "exports");
            outputTextBox.Dock = DockStyle.Fill;
            root.Controls.Add(outputTextBox, 1, 2);

            Button outputBrowseButton = new Button();
            outputBrowseButton.Text = "찾아보기";
            outputBrowseButton.Dock = DockStyle.Fill;
            outputBrowseButton.Click += BrowseOutput;
            root.Controls.Add(outputBrowseButton, 2, 2);

            copySdCheckBox = new CheckBox();
            copySdCheckBox.Text = "연결된 SD 카드에서 .log 파일도 자동 복사";
            copySdCheckBox.Checked = true;
            copySdCheckBox.Dock = DockStyle.Fill;
            copySdCheckBox.CheckedChanged += delegate
            {
                sdPathTextBox.Enabled = copySdCheckBox.Checked;
                sdBrowseButton.Enabled = copySdCheckBox.Checked;
            };
            root.Controls.Add(copySdCheckBox, 0, 3);
            root.SetColumnSpan(copySdCheckBox, 3);

            root.Controls.Add(CreateLabel("SD 폴더(선택)"), 0, 4);
            sdPathTextBox = new TextBox();
            sdPathTextBox.Dock = DockStyle.Fill;
            sdPathTextBox.Text = string.Empty;
            root.Controls.Add(sdPathTextBox, 1, 4);

            sdBrowseButton = new Button();
            sdBrowseButton.Text = "찾아보기";
            sdBrowseButton.Dock = DockStyle.Fill;
            sdBrowseButton.Click += BrowseSd;
            root.Controls.Add(sdBrowseButton, 2, 4);

            Label note = new Label();
            note.Text = "SD 폴더를 비워두면 이동식 드라이브를 자동 탐색합니다. " +
                        "보드 원격 .log 전송은 현재 OCI TCP 80이 막혀 있어, SD 카드가 PC에 연결된 경우에만 자동 복사됩니다.";
            note.ForeColor = Color.FromArgb(100, 74, 0);
            note.Dock = DockStyle.Fill;
            note.AutoSize = false;
            note.TextAlign = ContentAlignment.MiddleLeft;
            root.Controls.Add(note, 0, 5);
            root.SetColumnSpan(note, 3);

            downloadButton = new Button();
            downloadButton.Text = "지금 다운로드";
            downloadButton.Font = new Font(Font.FontFamily, 10F, FontStyle.Bold);
            downloadButton.Dock = DockStyle.Fill;
            downloadButton.Click += async delegate { await StartDownloadAsync(); };
            root.Controls.Add(downloadButton, 0, 6);
            root.SetColumnSpan(downloadButton, 3);

            progressBar = new ProgressBar();
            progressBar.Dock = DockStyle.Fill;
            progressBar.Style = ProgressBarStyle.Blocks;
            root.Controls.Add(progressBar, 0, 7);
            root.SetColumnSpan(progressBar, 3);

            logTextBox = new TextBox();
            logTextBox.Dock = DockStyle.Fill;
            logTextBox.Multiline = true;
            logTextBox.ReadOnly = true;
            logTextBox.ScrollBars = ScrollBars.Vertical;
            logTextBox.Font = new Font("Consolas", 9F);
            logTextBox.BackColor = Color.White;
            root.Controls.Add(logTextBox, 0, 8);
            root.SetColumnSpan(logTextBox, 3);

            statusLabel = new Label();
            statusLabel.Text = "실행하면 자동으로 오늘 데이터를 내려받습니다.";
            statusLabel.Dock = DockStyle.Fill;
            statusLabel.TextAlign = ContentAlignment.MiddleLeft;
            root.Controls.Add(statusLabel, 0, 9);
            root.SetColumnSpan(statusLabel, 3);

            Shown += async delegate
            {
                await Task.Delay(350);
                await StartDownloadAsync();
            };
        }

        private static Label CreateLabel(string text)
        {
            Label label = new Label();
            label.Text = text;
            label.Dock = DockStyle.Fill;
            label.TextAlign = ContentAlignment.MiddleLeft;
            return label;
        }

        private void BrowseOutput(object sender, EventArgs e)
        {
            using (FolderBrowserDialog dialog = new FolderBrowserDialog())
            {
                dialog.Description = "CSV와 .log 파일을 저장할 폴더";
                dialog.SelectedPath = outputTextBox.Text;
                if (dialog.ShowDialog(this) == DialogResult.OK)
                {
                    outputTextBox.Text = dialog.SelectedPath;
                }
            }
        }

        private void BrowseSd(object sender, EventArgs e)
        {
            using (FolderBrowserDialog dialog = new FolderBrowserDialog())
            {
                dialog.Description = "TMA SD 카드 또는 .log가 있는 폴더 (비워두면 자동 탐색)";
                dialog.SelectedPath = sdPathTextBox.Text;
                if (dialog.ShowDialog(this) == DialogResult.OK)
                {
                    sdPathTextBox.Text = dialog.SelectedPath;
                }
            }
        }

        private async Task StartDownloadAsync()
        {
            if (running)
            {
                return;
            }

            string output = outputTextBox.Text.Trim();
            if (output.Length == 0)
            {
                MessageBox.Show(this, "저장 폴더를 지정해주세요.", "LIGHT_ON", MessageBoxButtons.OK,
                    MessageBoxIcon.Warning);
                return;
            }

            running = true;
            downloadButton.Enabled = false;
            datePicker.Enabled = false;
            outputTextBox.Enabled = false;
            copySdCheckBox.Enabled = false;
            sdPathTextBox.Enabled = false;
            sdBrowseButton.Enabled = false;
            progressBar.Style = ProgressBarStyle.Marquee;
            statusLabel.Text = "다운로드 중...";
            logTextBox.Clear();

            DateTime selectedDate = datePicker.Value.Date;
            bool copySd = copySdCheckBox.Checked;
            string sdPath = sdPathTextBox.Text.Trim();

            try
            {
                DownloadEngine engine = new DownloadEngine(workspaceRoot, AppendLog);
                DownloadResult result = await Task.Run(delegate
                {
                    return engine.Run(selectedDate, output, copySd, sdPath);
                });

                if (result.CsvSucceeded)
                {
                    statusLabel.Text = "완료: " + result.CsvPath;
                    string summary = string.Format(CultureInfo.CurrentCulture,
                        "CSV 다운로드 완료\r\n{0}\r\n데이터 행: {1:N0}개\r\nSD .log 복사: {2:N0}개",
                        result.CsvPath, result.CsvRows, result.SdCopied);

                    if (result.ThermistorCsvSucceeded)
                    {
                        summary += string.Format(CultureInfo.CurrentCulture,
                            "\r\n45채널 온도 CSV: {0}\r\n온도 스냅샷: {1:N0}개",
                            result.ThermistorCsvPath,
                            result.ThermistorCsvRows);
                    }

                    if (result.Warnings.Count > 0)
                    {
                        summary += "\r\n\r\n참고:\r\n- " + string.Join("\r\n- ", result.Warnings.ToArray());
                    }

                    MessageBox.Show(this, summary, "LIGHT_ON 다운로드 완료", MessageBoxButtons.OK,
                        result.Warnings.Count == 0 ? MessageBoxIcon.Information : MessageBoxIcon.Warning);
                }
                else
                {
                    statusLabel.Text = "CSV 다운로드 실패";
                    MessageBox.Show(this, string.Join("\r\n", result.Errors.ToArray()),
                        "LIGHT_ON 다운로드 실패", MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            }
            catch (Exception ex)
            {
                AppendLog("실패: " + ex.Message);
                statusLabel.Text = "다운로드 실패";
                MessageBox.Show(this, ex.Message, "LIGHT_ON 다운로드 실패", MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
            }
            finally
            {
                running = false;
                downloadButton.Enabled = true;
                datePicker.Enabled = true;
                outputTextBox.Enabled = true;
                copySdCheckBox.Enabled = true;
                sdPathTextBox.Enabled = copySdCheckBox.Checked;
                sdBrowseButton.Enabled = copySdCheckBox.Checked;
                progressBar.Style = ProgressBarStyle.Blocks;
            }
        }

        private void AppendLog(string message)
        {
            if (InvokeRequired)
            {
                BeginInvoke(new Action<string>(AppendLog), message);
                return;
            }

            logTextBox.AppendText(string.Format(CultureInfo.CurrentCulture, "[{0:HH:mm:ss}] {1}\r\n",
                DateTime.Now, message));
        }
    }

    internal sealed class DownloadEngine
    {
        private const string Server = "168.110.48.114";
        private const string SshUser = "ubuntu";
        private const string DatabaseContainer = "monolith-v2-postgres-1";
        private const string DatabaseUser = "monolith";
        private const string DatabaseName = "monolith";
        private const int ThermistorChannelCount = 45;
        private const int ThermistorMeasurementSpanLimitMs = 6000;
        private const int ThermistorBoundaryPaddingSeconds = 35;

        private readonly string workspaceRoot;
        private readonly Action<string> log;
        private readonly string keyPath;
        private readonly string knownHostsPath;
        private readonly string sshPath;
        private readonly string scpPath;

        public DownloadEngine(string workspaceRoot, Action<string> log)
        {
            this.workspaceRoot = workspaceRoot;
            this.log = log ?? delegate(string ignored) { };
            keyPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
                ".ssh", "oracle_monolith.key");
            knownHostsPath = Path.Combine(workspaceRoot, ".oracle_known_hosts");
            sshPath = ResolveOpenSshTool("ssh.exe");
            scpPath = ResolveOpenSshTool("scp.exe");
        }

        public DownloadResult Run(DateTime date, string outputDirectory, bool copySd, string sdPath)
        {
            DownloadResult result = new DownloadResult();
            Directory.CreateDirectory(outputDirectory);

            try
            {
                ValidatePrerequisites();
                CsvExportResult csv = ExportCsv(date, outputDirectory);
                result.CsvSucceeded = true;
                result.CsvPath = csv.Path;
                result.CsvRows = csv.Rows;
                result.CsvBytes = csv.Bytes;

                try
                {
                    CsvExportResult thermistors = ExportThermistorCsv(date, outputDirectory);
                    result.ThermistorCsvSucceeded = true;
                    result.ThermistorCsvPath = thermistors.Path;
                    result.ThermistorCsvRows = thermistors.Rows;
                    result.ThermistorCsvBytes = thermistors.Bytes;
                    if (thermistors.Rows == 0)
                    {
                        result.Warnings.Add("선택한 날짜에 완성되거나 부분 수신된 TEM 온도 스냅샷이 없습니다.");
                    }
                }
                catch (Exception ex)
                {
                    /* 기존 telemetry CSV는 계속 유효하다. 서버 migration이 아직
                     * 배포되지 않은 경우에도 전체 다운로드를 실패시키지 않는다. */
                    result.Warnings.Add("45채널 온도 CSV 생성 실패: " + ex.Message);
                    log("45채널 온도 CSV 생성 실패: " + ex.Message);
                }
            }
            catch (Exception ex)
            {
                result.Errors.Add("CSV: " + ex.Message);
                log("CSV 다운로드 실패: " + ex.Message);
            }

            if (copySd)
            {
                try
                {
                    SdCopyResult sd = CopySdLogs(date, outputDirectory, sdPath);
                    result.SdCopied = sd.Copied;
                    result.SdSkipped = sd.Skipped;
                    result.SdBytes = sd.Bytes;
                    if (sd.RootsScanned == 0)
                    {
                        result.Warnings.Add("연결된 SD 카드를 찾지 못해 .log는 복사하지 않았습니다.");
                    }
                    else if (sd.Found == 0)
                    {
                        result.Warnings.Add("SD 카드에서 .log 파일을 찾지 못했습니다.");
                    }
                }
                catch (Exception ex)
                {
                    result.Warnings.Add("SD .log 복사 실패: " + ex.Message);
                    log("SD .log 복사 실패: " + ex.Message);
                }
            }

            return result;
        }

        private void ValidatePrerequisites()
        {
            if (!File.Exists(keyPath))
            {
                throw new FileNotFoundException("SSH 키를 찾을 수 없습니다.", keyPath);
            }
            if (!File.Exists(knownHostsPath))
            {
                throw new FileNotFoundException("서버 host key 파일을 찾을 수 없습니다.", knownHostsPath);
            }
            if (!File.Exists(sshPath) || !File.Exists(scpPath))
            {
                throw new FileNotFoundException("Windows OpenSSH(ssh.exe/scp.exe)를 찾을 수 없습니다.");
            }
        }

        private CsvExportResult ExportCsv(DateTime date, string outputDirectory)
        {
            string dateText = date.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture);
            string nextDateText = date.AddDays(1).ToString("yyyy-MM-dd", CultureInfo.InvariantCulture);
            return ExportQueryCsv(dateText, outputDirectory,
                "telemetry", "telemetry", BuildCsvSql(dateText, nextDateText));
        }

        private CsvExportResult ExportThermistorCsv(DateTime date, string outputDirectory)
        {
            string dateText = date.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture);
            string nextDateText = date.AddDays(1).ToString("yyyy-MM-dd", CultureInfo.InvariantCulture);
            return ExportQueryCsv(dateText, outputDirectory,
                "thermistors", "45채널 TEM 온도",
                BuildThermistorCsvSql(dateText, nextDateText));
        }

        private CsvExportResult ExportQueryCsv(string dateText,
            string outputDirectory, string filePrefix, string description, string sql)
        {
            string nonce = Guid.NewGuid().ToString("N");
            string remotePath = "/home/ubuntu/monolith-" + filePrefix + "-" +
                dateText + "-" + nonce + ".csv";
            string localPath = MakeUniquePath(Path.Combine(outputDirectory,
                filePrefix + "-" + dateText + ".csv"));
            string localPartPath = localPath + ".part-" + nonce;
            string sqlBase64 = Convert.ToBase64String(Encoding.UTF8.GetBytes(sql));
            string remoteCommand = "umask 077; printf %s '" + sqlBase64 +
                "' | base64 -d | docker exec -i " + DatabaseContainer +
                " psql -X -q -v ON_ERROR_STOP=1 -U " + DatabaseUser + " -d " + DatabaseName +
                " > '" + remotePath + "'";

            log(dateText + " " + description + " 데이터를 CSV로 내보내는 중...");
            try
            {
                CommandResult export = RunProcess(sshPath,
                    BuildSshArguments(SshUser + "@" + Server, remoteCommand), 180000);
                EnsureSuccess(export, "서버 CSV 생성");

                log("CSV를 PC로 복사하는 중...");
                CommandResult copy = RunProcess(scpPath,
                    BuildScpArguments(SshUser + "@" + Server + ":" + remotePath, localPartPath), 180000);
                EnsureSuccess(copy, "CSV SCP 다운로드");

                FileInfo file = new FileInfo(localPartPath);
                if (!file.Exists || file.Length == 0)
                {
                    throw new IOException("다운로드된 CSV가 비어 있습니다.");
                }

                long rows = CountLines(localPartPath) - 1;
                if (rows < 0)
                {
                    rows = 0;
                }
                File.Move(localPartPath, localPath);
                file = new FileInfo(localPath);
                log(string.Format(CultureInfo.CurrentCulture,
                    "CSV 완료: {0:N0}행, {1}", rows, FormatBytes(file.Length)));

                CsvExportResult result = new CsvExportResult();
                result.Path = localPath;
                result.Rows = rows;
                result.Bytes = file.Length;
                return result;
            }
            catch
            {
                TryDelete(localPartPath);
                TryDelete(localPath);
                throw;
            }
            finally
            {
                string cleanupCommand = "rm -f -- '" + remotePath + "'";
                try
                {
                    RunProcess(sshPath,
                        BuildSshArguments(SshUser + "@" + Server, cleanupCommand), 30000);
                }
                catch
                {
                    log("경고: 서버 임시 CSV 자동 정리를 확인하지 못했습니다.");
                }
            }
        }

        private SdCopyResult CopySdLogs(DateTime date, string outputDirectory, string selectedPath)
        {
            List<string> roots = new List<string>();
            if (!string.IsNullOrWhiteSpace(selectedPath))
            {
                string fullPath = Path.GetFullPath(selectedPath);
                if (!Directory.Exists(fullPath))
                {
                    throw new DirectoryNotFoundException("선택한 SD 폴더가 없습니다: " + fullPath);
                }
                roots.Add(fullPath);
            }
            else
            {
                foreach (DriveInfo drive in DriveInfo.GetDrives())
                {
                    try
                    {
                        if (drive.IsReady && drive.DriveType == DriveType.Removable)
                        {
                            roots.Add(drive.RootDirectory.FullName);
                        }
                    }
                    catch
                    {
                        // 준비되지 않은 카드 리더는 건너뛴다.
                    }
                }
            }

            SdCopyResult result = new SdCopyResult();
            result.RootsScanned = roots.Count;
            if (roots.Count == 0)
            {
                log("이동식 SD 카드가 없어 .log 복사를 건너뜁니다.");
                return result;
            }

            string destinationRoot = Path.Combine(outputDirectory, "logs",
                date.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture));

            foreach (string root in roots.Distinct(StringComparer.OrdinalIgnoreCase))
            {
                log(".log 검색: " + root);
                foreach (string source in EnumerateLogFilesSafely(root))
                {
                    result.Found++;
                    string rootName = BuildRootName(root);
                    string relativePath = MakeRelativePath(root, source);
                    string targetDirectory = Path.Combine(destinationRoot, rootName,
                        Path.GetDirectoryName(relativePath) ?? string.Empty);
                    Directory.CreateDirectory(targetDirectory);
                    string target = Path.Combine(targetDirectory, Path.GetFileName(relativePath));

                    FileInfo sourceInfo = new FileInfo(source);
                    if (File.Exists(target))
                    {
                        FileInfo targetInfo = new FileInfo(target);
                        if (targetInfo.Length == sourceInfo.Length && FilesHaveSameHash(source, target))
                        {
                            result.Skipped++;
                            continue;
                        }
                        target = MakeUniquePath(target);
                    }

                    string partPath = target + ".part-" + Guid.NewGuid().ToString("N");
                    long beforeLength = sourceInfo.Length;
                    DateTime beforeWrite = sourceInfo.LastWriteTimeUtc;
                    try
                    {
                        File.Copy(source, partPath, false);
                        sourceInfo.Refresh();
                        FileInfo partInfo = new FileInfo(partPath);
                        if (sourceInfo.Length != beforeLength || sourceInfo.LastWriteTimeUtc != beforeWrite ||
                            partInfo.Length != beforeLength)
                        {
                            result.Skipped++;
                            log("기록 중이거나 변경된 .log 건너뜀: " + Path.GetFileName(source));
                            continue;
                        }

                        File.Move(partPath, target);
                        File.SetLastWriteTimeUtc(target, beforeWrite);
                        result.Copied++;
                        result.Bytes += beforeLength;
                        log(".log 복사: " + Path.GetFileName(source));
                    }
                    finally
                    {
                        TryDelete(partPath);
                    }
                }
            }

            log(string.Format(CultureInfo.CurrentCulture,
                "SD .log 완료: {0:N0}개 복사, {1:N0}개 건너뜀, {2}",
                result.Copied, result.Skipped, FormatBytes(result.Bytes)));
            return result;
        }

        private static string BuildCsvSql(string dateText, string nextDateText)
        {
            return string.Format(CultureInfo.InvariantCulture, @"COPY (
  SELECT
    s.session_id,
    s.received_at AT TIME ZONE 'Asia/Seoul' AS received_at_kst,
    s.device_ts_ms,
    s.kind,
    s.latitude,
    s.longitude,
    s.speed,
    s.course,
    s.ain[1] AS ain1_raw,
    s.ain[2] AS ain2_raw,
    s.ain[3] AS ain3_raw,
    s.ain[4] AS ain4_raw,
    s.ain[5] AS ain5_raw,
    s.ain[6] AS ain6_raw,
    round(s.ain[1] / 8000.0, 6) AS ain1_v,
    round(s.ain[2] / 8000.0, 6) AS ain2_v,
    round(s.ain[3] / 8000.0, 6) AS ain3_v,
    round(s.ain[4] / 8000.0, 6) AS ain4_v,
    round(s.ain[5] / 8000.0, 6) AS ain5_v,
    round(s.ain[6] / 8000.0, 6) AS ain6_v,
    s.voltage AS power_raw,
    round(s.voltage / 8000.0 * (15430.0 / 430.0), 6) AS power_v,
    s.temperature AS temperature_raw,
    round(s.temperature / 100.0, 2) AS temperature_c,
    s.din[1] AS din1,
    s.din[2] AS din2,
    s.din[3] AS din3,
    s.din[4] AS din4,
    s.accel[1] AS accel_x_raw,
    s.accel[2] AS accel_y_raw,
    s.accel[3] AS accel_z_raw,
    round(s.accel[1] / 4096.0, 6) AS accel_x_g,
    round(s.accel[2] / 4096.0, 6) AS accel_y_g,
    round(s.accel[3] / 4096.0, 6) AS accel_z_g,
    s.gyro[1] AS gyro_x_raw,
    s.gyro[2] AS gyro_y_raw,
    s.gyro[3] AS gyro_z_raw,
    round(s.gyro[1] / 32768.0 * 500.0, 6) AS gyro_x_dps,
    round(s.gyro[2] / 32768.0 * 500.0, 6) AS gyro_y_dps,
    round(s.gyro[3] / 32768.0 * 500.0, 6) AS gyro_z_dps
  FROM samples AS s
  WHERE s.received_at >= timestamptz '{0} 00:00:00 Asia/Seoul'
    AND s.received_at <  timestamptz '{1} 00:00:00 Asia/Seoul'
  ORDER BY s.received_at, s.id
) TO STDOUT WITH (FORMAT CSV, HEADER TRUE, ENCODING 'UTF8');
", dateText, nextDateText);
        }

        private static string BuildThermistorCsvSql(string dateText, string nextDateText)
        {
            StringBuilder channels = new StringBuilder();
            for (int sensor = 0; sensor < ThermistorChannelCount; sensor++)
            {
                channels.AppendFormat(CultureInfo.InvariantCulture,
                    ",\n    max(temperature_c) FILTER (WHERE sample_rank = 1 AND sensor_id = {0}) AS temp_{1:00}_c",
                    sensor, sensor + 1);
            }
            for (int sensor = 0; sensor < ThermistorChannelCount; sensor++)
            {
                channels.AppendFormat(CultureInfo.InvariantCulture,
                    ",\n    bool_or(fault) FILTER (WHERE sample_rank = 1 AND sensor_id = {0}) AS fault_{1:00}",
                    sensor, sensor + 1);
            }
            for (int sensor = 0; sensor < ThermistorChannelCount; sensor++)
            {
                channels.AppendFormat(CultureInfo.InvariantCulture,
                    ",\n    max(device_ts_ms) FILTER (WHERE sample_rank = 1 AND sensor_id = {0}) AS measurement_device_ts_{1:00}_ms",
                    sensor, sensor + 1);
            }

            return @"COPY (
  WITH stream AS (
    SELECT
      t.*,
      d.channel,
      lag(t.sensor_id) OVER (
        PARTITION BY t.session_id, t.module_address
        ORDER BY t.received_at, t.id
      ) AS previous_sensor_id,
      lag(t.device_ts_ms) OVER (
        PARTITION BY t.session_id, t.module_address
        ORDER BY t.received_at, t.id
      ) AS previous_device_ts_ms
    FROM thermistor_samples AS t
    JOIN sessions AS s ON s.id = t.session_id
    JOIN devices AS d ON d.id = s.device_id
    WHERE t.received_at >= timestamptz '" + dateText + @" 00:00:00 Asia/Seoul' - interval '" +
                ThermistorBoundaryPaddingSeconds.ToString(CultureInfo.InvariantCulture) + @" seconds'
      AND t.received_at <  timestamptz '" + nextDateText + @" 00:00:00 Asia/Seoul' + interval '" +
                ThermistorBoundaryPaddingSeconds.ToString(CultureInfo.InvariantCulture) + @" seconds'
      AND t.module_address = 128
      AND t.sensor_id BETWEEN 0 AND 44
  ), ordered AS (
    SELECT
      stream.*,
      sum(CASE
            WHEN previous_sensor_id IS NULL
              OR sensor_id < previous_sensor_id
              OR ((device_ts_ms - previous_device_ts_ms + 4294967296::bigint)
                % 4294967296::bigint) > " +
                ThermistorMeasurementSpanLimitMs.ToString(CultureInfo.InvariantCulture) + @" THEN 1
            ELSE 0
          END) OVER (
        PARTITION BY session_id, module_address
        ORDER BY received_at, id
        ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
      ) AS scan_no
    FROM stream
  ), ranked AS (
    SELECT
      ordered.*,
      row_number() OVER (
        PARTITION BY session_id, module_address, scan_no, sensor_id
        ORDER BY received_at DESC, id DESC
      ) AS sample_rank
    FROM ordered
  ), scans AS (
    SELECT
      channel,
      session_id,
      module_address,
      scan_no,
      min(received_at) FILTER (WHERE sample_rank = 1) AS scan_started_at,
      max(received_at) FILTER (WHERE sample_rank = 1) AS scan_completed_at,
      (array_agg(device_ts_ms ORDER BY received_at, id)
        FILTER (WHERE sample_rank = 1))[1] AS device_ts_start_ms,
      (array_agg(device_ts_ms ORDER BY received_at DESC, id DESC)
        FILTER (WHERE sample_rank = 1))[1] AS device_ts_end_ms,
      count(*) FILTER (WHERE sample_rank = 1) AS received_sensor_count,
      count(*) FILTER (WHERE sample_rank = 1 AND fault) AS fault_sensor_count,
      max(temperature_c) FILTER (
        WHERE sample_rank = 1 AND NOT fault
      ) AS max_temperature_c,
      (array_agg(sensor_id + 1 ORDER BY temperature_c DESC, sensor_id)
        FILTER (WHERE sample_rank = 1 AND NOT fault))[1] AS max_sensor_id" +
                channels.ToString() + @"
    FROM ranked
    GROUP BY channel, session_id, module_address, scan_no
  )
  SELECT
    channel,
    session_id,
    '0x' || lpad(to_hex(module_address::integer), 2, '0') AS module_address,
    scan_no AS snapshot_no,
    scan_started_at AT TIME ZONE 'Asia/Seoul' AS server_delivery_started_at_kst,
    scan_completed_at AT TIME ZONE 'Asia/Seoul' AS server_delivery_completed_at_kst,
    round(extract(epoch FROM (scan_completed_at - scan_started_at)) * 1000)::bigint
      AS server_delivery_duration_ms,
    device_ts_start_ms AS measurement_started_device_ts_ms,
    device_ts_end_ms AS measurement_completed_device_ts_ms,
    ((device_ts_end_ms - device_ts_start_ms + 4294967296::bigint)
      % 4294967296::bigint) AS measurement_span_ms,
    received_sensor_count,
    fault_sensor_count,
    (
      received_sensor_count = " + ThermistorChannelCount.ToString(CultureInfo.InvariantCulture) + @"
      AND ((device_ts_end_ms - device_ts_start_ms + 4294967296::bigint)
        % 4294967296::bigint) <= " +
                ThermistorMeasurementSpanLimitMs.ToString(CultureInfo.InvariantCulture) + @"
    ) AS complete,
    max_temperature_c,
    max_sensor_id" +
                BuildThermistorChannelSelectList() + @"
  FROM scans
  WHERE scan_completed_at >= timestamptz '" + dateText + @" 00:00:00 Asia/Seoul'
    AND scan_completed_at <  timestamptz '" + nextDateText + @" 00:00:00 Asia/Seoul'
  ORDER BY scan_started_at, session_id, module_address, scan_no
) TO STDOUT WITH (FORMAT CSV, HEADER TRUE, ENCODING 'UTF8');
";
        }

        private static string BuildThermistorChannelSelectList()
        {
            StringBuilder columns = new StringBuilder();
            for (int sensor = 1; sensor <= ThermistorChannelCount; sensor++)
            {
                columns.AppendFormat(CultureInfo.InvariantCulture,
                    ",\n    temp_{0:00}_c", sensor);
            }
            for (int sensor = 1; sensor <= ThermistorChannelCount; sensor++)
            {
                columns.AppendFormat(CultureInfo.InvariantCulture,
                    ",\n    fault_{0:00}", sensor);
            }
            for (int sensor = 1; sensor <= ThermistorChannelCount; sensor++)
            {
                columns.AppendFormat(CultureInfo.InvariantCulture,
                    ",\n    measurement_device_ts_{0:00}_ms", sensor);
            }
            return columns.ToString();
        }

        private List<string> BuildSshArguments(string target, string remoteCommand)
        {
            List<string> arguments = BuildCommonArguments();
            arguments.Add(target);
            arguments.Add(remoteCommand);
            return arguments;
        }

        private List<string> BuildScpArguments(string source, string destination)
        {
            List<string> arguments = BuildCommonArguments();
            arguments.Add(source);
            arguments.Add(destination);
            return arguments;
        }

        private List<string> BuildCommonArguments()
        {
            return new List<string>
            {
                "-i", keyPath,
                "-o", "BatchMode=yes",
                "-o", "IdentitiesOnly=yes",
                "-o", "StrictHostKeyChecking=yes",
                "-o", "ConnectTimeout=10",
                "-o", "ServerAliveInterval=15",
                "-o", "ServerAliveCountMax=2",
                "-o", "UserKnownHostsFile=" + knownHostsPath
            };
        }

        private static CommandResult RunProcess(string executable, IList<string> arguments, int timeoutMs)
        {
            ProcessStartInfo startInfo = new ProcessStartInfo();
            startInfo.FileName = executable;
            startInfo.Arguments = string.Join(" ", arguments.Select(QuoteWindowsArgument).ToArray());
            startInfo.UseShellExecute = false;
            startInfo.CreateNoWindow = true;
            startInfo.RedirectStandardOutput = true;
            startInfo.RedirectStandardError = true;
            startInfo.StandardOutputEncoding = Encoding.UTF8;
            startInfo.StandardErrorEncoding = Encoding.UTF8;

            using (Process process = new Process())
            {
                process.StartInfo = startInfo;
                if (!process.Start())
                {
                    throw new InvalidOperationException("실행하지 못했습니다: " + executable);
                }

                Task<string> stdout = process.StandardOutput.ReadToEndAsync();
                Task<string> stderr = process.StandardError.ReadToEndAsync();
                bool exited = process.WaitForExit(timeoutMs);
                if (!exited)
                {
                    try { process.Kill(); } catch { }
                    throw new TimeoutException(Path.GetFileName(executable) + " 실행 시간이 초과되었습니다.");
                }
                process.WaitForExit();
                Task.WaitAll(new Task[] { stdout, stderr }, 5000);

                CommandResult result = new CommandResult();
                result.ExitCode = process.ExitCode;
                result.StandardOutput = stdout.Result;
                result.StandardError = stderr.Result;
                return result;
            }
        }

        private static void EnsureSuccess(CommandResult result, string operation)
        {
            if (result.ExitCode == 0)
            {
                return;
            }

            string detail = FirstUsefulLine(result.StandardError);
            if (detail.Length == 0)
            {
                detail = FirstUsefulLine(result.StandardOutput);
            }
            if (detail.Length == 0)
            {
                detail = "exit code " + result.ExitCode.ToString(CultureInfo.InvariantCulture);
            }
            throw new InvalidOperationException(operation + " 실패: " + detail);
        }

        private static string FirstUsefulLine(string value)
        {
            if (string.IsNullOrWhiteSpace(value))
            {
                return string.Empty;
            }
            string[] lines = value.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
            return lines.Length == 0 ? string.Empty : lines[0].Trim();
        }

        private static string QuoteWindowsArgument(string value)
        {
            if (value == null || value.Length == 0)
            {
                return "\"\"";
            }
            if (!value.Any(delegate(char c) { return char.IsWhiteSpace(c) || c == '\"'; }))
            {
                return value;
            }

            StringBuilder result = new StringBuilder();
            result.Append('\"');
            int slashCount = 0;
            foreach (char c in value)
            {
                if (c == '\\')
                {
                    slashCount++;
                }
                else if (c == '\"')
                {
                    result.Append('\\', slashCount * 2 + 1);
                    result.Append('\"');
                    slashCount = 0;
                }
                else
                {
                    result.Append('\\', slashCount);
                    slashCount = 0;
                    result.Append(c);
                }
            }
            result.Append('\\', slashCount * 2);
            result.Append('\"');
            return result.ToString();
        }

        private static IEnumerable<string> EnumerateLogFilesSafely(string root)
        {
            Stack<string> pending = new Stack<string>();
            pending.Push(root);

            while (pending.Count > 0)
            {
                string directory = pending.Pop();
                string name = Path.GetFileName(directory.TrimEnd(Path.DirectorySeparatorChar));
                if (string.Equals(name, "System Volume Information", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(name, "$RECYCLE.BIN", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                string[] files;
                try { files = Directory.GetFiles(directory); }
                catch { files = new string[0]; }

                foreach (string file in files)
                {
                    if (string.Equals(Path.GetExtension(file), ".log", StringComparison.OrdinalIgnoreCase))
                    {
                        yield return file;
                    }
                }

                string[] directories;
                try { directories = Directory.GetDirectories(directory); }
                catch { directories = new string[0]; }

                foreach (string child in directories)
                {
                    try
                    {
                        if ((File.GetAttributes(child) & FileAttributes.ReparsePoint) == 0)
                        {
                            pending.Push(child);
                        }
                    }
                    catch { }
                }
            }
        }

        private static string MakeRelativePath(string root, string file)
        {
            string normalizedRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar,
                Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
            string normalizedFile = Path.GetFullPath(file);
            if (normalizedFile.StartsWith(normalizedRoot, StringComparison.OrdinalIgnoreCase))
            {
                return normalizedFile.Substring(normalizedRoot.Length);
            }
            return Path.GetFileName(normalizedFile);
        }

        private static bool FilesHaveSameHash(string first, string second)
        {
            using (SHA256 sha = SHA256.Create())
            using (FileStream firstStream = File.OpenRead(first))
            using (FileStream secondStream = File.OpenRead(second))
            {
                byte[] firstHash = sha.ComputeHash(firstStream);
                sha.Initialize();
                byte[] secondHash = sha.ComputeHash(secondStream);
                return firstHash.SequenceEqual(secondHash);
            }
        }

        private static string BuildRootName(string root)
        {
            string full = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar);
            string drive = Path.GetPathRoot(full) ?? string.Empty;
            string driveName = drive.Replace(":", string.Empty).Replace("\\", string.Empty);
            if (driveName.Length == 0)
            {
                driveName = "SD";
            }

            try
            {
                DriveInfo info = new DriveInfo(drive);
                if (info.IsReady && !string.IsNullOrWhiteSpace(info.VolumeLabel))
                {
                    driveName += "_" + info.VolumeLabel;
                }
            }
            catch { }

            return SanitizeFileName(driveName);
        }

        private static string SanitizeFileName(string value)
        {
            foreach (char invalid in Path.GetInvalidFileNameChars())
            {
                value = value.Replace(invalid, '_');
            }
            return value;
        }

        private static string MakeUniquePath(string path)
        {
            if (!File.Exists(path))
            {
                return path;
            }

            string directory = Path.GetDirectoryName(path) ?? string.Empty;
            string name = Path.GetFileNameWithoutExtension(path);
            string extension = Path.GetExtension(path);
            for (int i = 2; i < 10000; i++)
            {
                string candidate = Path.Combine(directory, name + "-" + i.ToString(CultureInfo.InvariantCulture) + extension);
                if (!File.Exists(candidate))
                {
                    return candidate;
                }
            }
            throw new IOException("고유한 파일 이름을 만들 수 없습니다: " + path);
        }

        private static long CountLines(string path)
        {
            long count = 0;
            using (StreamReader reader = new StreamReader(path, Encoding.UTF8, true))
            {
                while (reader.ReadLine() != null)
                {
                    count++;
                }
            }
            return count;
        }

        private static string ResolveOpenSshTool(string fileName)
        {
            string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            string systemTool = Path.Combine(windows, "System32", "OpenSSH", fileName);
            return File.Exists(systemTool) ? systemTool : fileName;
        }

        private static void TryDelete(string path)
        {
            try
            {
                if (File.Exists(path))
                {
                    File.Delete(path);
                }
            }
            catch { }
        }

        private static string FormatBytes(long bytes)
        {
            string[] units = { "B", "KB", "MB", "GB" };
            double value = bytes;
            int unit = 0;
            while (value >= 1024.0 && unit < units.Length - 1)
            {
                value /= 1024.0;
                unit++;
            }
            return string.Format(CultureInfo.CurrentCulture, "{0:0.##} {1}", value, units[unit]);
        }
    }

    internal static class WorkspaceLocator
    {
        public static string Find()
        {
            string[] starts =
            {
                AppDomain.CurrentDomain.BaseDirectory,
                Environment.CurrentDirectory
            };

            foreach (string start in starts)
            {
                DirectoryInfo directory = new DirectoryInfo(start);
                while (directory != null)
                {
                    if (File.Exists(Path.Combine(directory.FullName, ".oracle_known_hosts")) &&
                        File.Exists(Path.Combine(directory.FullName, "MONOLITH_DEPLOYMENT.md")))
                    {
                        return directory.FullName;
                    }
                    directory = directory.Parent;
                }
            }

            string knownWorkspace = @"C:\project\LIGHT_ON";
            return Directory.Exists(knownWorkspace) ? knownWorkspace : Environment.CurrentDirectory;
        }
    }

    internal sealed class DownloadResult
    {
        public bool CsvSucceeded;
        public string CsvPath = string.Empty;
        public long CsvRows;
        public long CsvBytes;
        public bool ThermistorCsvSucceeded;
        public string ThermistorCsvPath = string.Empty;
        public long ThermistorCsvRows;
        public long ThermistorCsvBytes;
        public int SdCopied;
        public int SdSkipped;
        public long SdBytes;
        public readonly List<string> Warnings = new List<string>();
        public readonly List<string> Errors = new List<string>();
    }

    internal sealed class CsvExportResult
    {
        public string Path = string.Empty;
        public long Rows;
        public long Bytes;
    }

    internal sealed class SdCopyResult
    {
        public int RootsScanned;
        public int Found;
        public int Copied;
        public int Skipped;
        public long Bytes;
    }

    internal sealed class CommandResult
    {
        public int ExitCode;
        public string StandardOutput = string.Empty;
        public string StandardError = string.Empty;
    }
}
