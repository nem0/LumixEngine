namespace editor_ng.FileServer
{
    partial class FileServerUI
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.fileList = new System.Windows.Forms.DataGridView();
            this.idxColumn = new System.Windows.Forms.DataGridViewTextBoxColumn();
            this.pathColumn = new System.Windows.Forms.DataGridViewTextBoxColumn();
            this.sizeColumn = new System.Windows.Forms.DataGridViewTextBoxColumn();
            ((System.ComponentModel.ISupportInitialize)(this.fileList)).BeginInit();
            this.SuspendLayout();
            // 
            // fileList
            // 
            this.fileList.AllowUserToAddRows = false;
            this.fileList.AllowUserToDeleteRows = false;
            this.fileList.ColumnHeadersHeightSizeMode = System.Windows.Forms.DataGridViewColumnHeadersHeightSizeMode.AutoSize;
            this.fileList.Columns.AddRange(new System.Windows.Forms.DataGridViewColumn[] {
            this.idxColumn,
            this.pathColumn,
            this.sizeColumn});
            this.fileList.Dock = System.Windows.Forms.DockStyle.Fill;
            this.fileList.Location = new System.Drawing.Point(0, 0);
            this.fileList.Name = "fileList";
            this.fileList.RowHeadersVisible = false;
            this.fileList.Size = new System.Drawing.Size(284, 262);
            this.fileList.TabIndex = 0;
            // 
            // idxColumn
            // 
            this.idxColumn.AutoSizeMode = System.Windows.Forms.DataGridViewAutoSizeColumnMode.Fill;
            this.idxColumn.HeaderText = "Index";
            this.idxColumn.Name = "idxColumn";
            this.idxColumn.ReadOnly = true;
            // 
            // pathColumn
            // 
            this.pathColumn.AutoSizeMode = System.Windows.Forms.DataGridViewAutoSizeColumnMode.Fill;
            this.pathColumn.HeaderText = "path";
            this.pathColumn.Name = "pathColumn";
            this.pathColumn.ReadOnly = true;
            // 
            // sizeColumn
            // 
            this.sizeColumn.AutoSizeMode = System.Windows.Forms.DataGridViewAutoSizeColumnMode.Fill;
            this.sizeColumn.HeaderText = "File size";
            this.sizeColumn.Name = "sizeColumn";
            this.sizeColumn.ReadOnly = true;
            // 
            // FileServerUI
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(284, 262);
            this.Controls.Add(this.fileList);
            this.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(238)));
            this.Name = "FileServerUI";
            this.Text = "FileServerUI";
            ((System.ComponentModel.ISupportInitialize)(this.fileList)).EndInit();
            this.ResumeLayout(false);

        }

        #endregion

        private System.Windows.Forms.DataGridView fileList;
        private System.Windows.Forms.DataGridViewTextBoxColumn idxColumn;
        private System.Windows.Forms.DataGridViewTextBoxColumn pathColumn;
        private System.Windows.Forms.DataGridViewTextBoxColumn sizeColumn;

    }
}