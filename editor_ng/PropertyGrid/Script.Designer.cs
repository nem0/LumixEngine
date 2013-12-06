namespace editor_ng.PropertyGrid
{
    partial class Script
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

        #region Component Designer generated code

        /// <summary> 
        /// Required method for Designer support - do not modify 
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.sourceFileInput = new editor_ng.PropertyGrid.FileInput();
            this.label1 = new System.Windows.Forms.Label();
            this.SuspendLayout();
            // 
            // sourceFileInput
            // 
            this.sourceFileInput.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.sourceFileInput.Filter = null;
            this.sourceFileInput.Location = new System.Drawing.Point(54, 0);
            this.sourceFileInput.Name = "sourceFileInput";
            this.sourceFileInput.Size = new System.Drawing.Size(286, 29);
            this.sourceFileInput.TabIndex = 0;
            this.sourceFileInput.Value = "";
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(5, 5);
            this.label1.Margin = new System.Windows.Forms.Padding(5);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(41, 13);
            this.label1.TabIndex = 1;
            this.label1.Text = "Source";
            // 
            // Script
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.Controls.Add(this.label1);
            this.Controls.Add(this.sourceFileInput);
            this.Name = "Script";
            this.Size = new System.Drawing.Size(340, 23);
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private FileInput sourceFileInput;
        private System.Windows.Forms.Label label1;
    }
}
