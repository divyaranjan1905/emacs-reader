;;; reader.el --- Reading Documents in Emacs  -*- lexical-binding: t; -*-

;; Copyright (c) 2025 Divya Ranjan Pattanaik <divya@subvertising.org>

;; Author: Divya Ranjan Pattanaik <divya@subvertising.org>
;; Keywords: document reader, mupdf
;; Version: 0.1.0
;; Package-Requires: ((emacs "24.1"))
;; URL: https://codeberg.org/divyaranjan/emacs-reader

;; This file is not part of GNU Emacs.

;; This program is free software; you can redistribute it and/or modify it under
;; the terms of the GNU General Public License as published by the Free Software
;; Foundation, either version 3 of the License, or (at your option) any later
;; version.

;; This program is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
;; FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
;; details.

;; You should have received a copy of the GNU General Public License along with
;; this program. If not, see <https://www.gnu.org/licenses/>.

;; In addition to conditions of the GNU General Public License, this program may
;; not be redistributed without the following acknowledgement:

;;; Commentary:

;;; Code:
(require 'image)
(require 'svg)
(require 'render-core)

(defgroup reader nil
  "Group for Reader’s customizations."
  :prefix "reader-"
  :group 'custom)

;; User-facing function to open a PDF
(defun read-pdf (pdf-file)
  "Open PDF-FILE for viewing.
This function calls the C function 'load-pdf' from the dynamic module
to render the first page and display it in a new buffer."
  (interactive "fFind PDF file: ")
  (unless (fboundp 'load-pdf)
    (error "The 'load-pdf' function from the dynamic module is not available."))
  (switch-to-buffer (create-file-buffer pdf-file))
  (insert "\n")
  (init-svg-overlay)
  (load-pdf (expand-file-name pdf-file))
  (read-pdf-mode))

(defun read-pdf--next-page ()
  "Go to the next page of the PDF."
  (interactive)
  (unless (fboundp 'next-pdf-page)
    (error "The 'next-pdf-page' function from the dynamic module is not available."))
  (let ((inhibit-read-only t))
    (next-pdf-page)))

(defun read-pdf--previous-page ()
  "Go to the previous page of the PDF."
  (interactive)
  (unless (fboundp 'previous-pdf-page)
    (error "The 'previous-pdf-page' function from the dynamic module is not available."))
  (let ((inhibit-read-only t))
    (previous-pdf-page)))

(defun read-pdf--first-page ()
  "Go to the first page of the PDF"
  (interactive)
  (unless (fboundp 'first-pdf-page)
    (error "The 'first-pdf-page' function from the dynamic module is not available."))
  (let ((inhibit-read-only t))
    (first-pdf-page)))

(defun read-pdf--last-page ()
  "Go to the last page of the PDF."
  (interactive)
  (unless (fboundp 'last-pdf-page)
    (error "The 'last-pdf-page' function from the dynamic module is not available."))
  (let ((inhibit-read-only t))
    (last-pdf-page)))

(defun read-pdf--kill-buffer ()
  "Kill the buffer with the rendered PDF"
  (interactive)
  (kill-buffer (current-buffer)))

(defun read-pdf--render-buffer ()
  "Render the file this buffer is associated with."
  (let ((file (buffer-file-name (current-buffer))))
    (if file
        (progn
	  (load-pdf file)
	  (read-pdf-mode))
      (message "No file associated with buffer."))))

;; Define the keymap for read-pdf-mode
(defvar read-pdf-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map (kbd "n") #'read-pdf--next-page)
    (define-key map (kbd "j") #'read-pdf--next-page)
    (define-key map (kbd "p") #'read-pdf--previous-page)
    (define-key map (kbd "k") #'read-pdf--previous-page)
    (define-key map (kbd "gg") #'read-pdf--first-page)
    (define-key map (kbd "M-<") #'read-pdf--first-page)
    (define-key map (kbd "G") #'read-pdf--last-page)
    (define-key map (kbd "M->") #'read-pdf--last-page)
    (define-key map (kbd "Q") #'read-pdf--kill-buffer)
    map)
  "Keymap for read-pdf-mode.")

;; Define the major mode
(define-derived-mode read-pdf-mode special-mode "ReadPDF"
  "Major mode for viewing PDFs rendered by render-pdf module.

Keybindings:
\\{read-pdf-mode-map}"
  ;; Make the buffer read-only to prevent editing the SVG image
  ;; (read-pdf--reload)
  (setq-local buffer-read-only t)
  (setq-local cursor-type nil)
  (blink-cursor-mode 0)
  (read-pdf--render-buffer))

;; Automatically load the mode
(add-to-list 'auto-mode-alist '("\\.pdf\\'" . read-pdf-mode))

(provide 'reader)
;;; reader.el ends here.
