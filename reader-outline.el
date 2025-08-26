;;; reader-outline.el --- imenu and outline-mode integration for the emacs-reader  -*- lexical-binding: t; -*-

;; Copyright (C) 2025  Divya Ranjan Pattanaik
;; Copyright (C) 2025  Tushar

;; This program is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation, either version 3 of the License, or
;; (at your option) any later version.

;; This program is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with this program.  If not, see <https://www.gnu.org/licenses/>.

;;; Commentary:

;;; Code:

(require 'reader)
(require 'imenu)
(require 'outline)

;;; Imenu

;;;###autoload
(defun reader--outline-make-imenu-entry (plist)
  "Convert one outline PLIST to an imenu entry."
  (let* ((title (plist-get plist :title))
         (page (1+ (plist-get plist :page))) ; MuPDF does 0-indexing
         (children (plist-get plist :children)))
    (if children
        (cons title
              (mapcar #'reader--outline-make-imenu-entry children))
      (cons title page))))

;;;###autoload
(defun reader--outline-imenu-create-index ()
  "Turn `reader-current-doc-outline' into an imenu index."
  (if reader-current-doc-outline
      (mapcar #'reader--outline-make-imenu-entry
	      reader-current-doc-outline)
    (error "Document lacks a proper outline!")))

;;;###autoload
(defun reader--outline-imenu-goto (_name page)
  "Switch to PAGE.

Ignores the _NAME argument.
Just wraps `reader-goto-page' for imenu compatibility."
  (reader-goto-page page))

;;; Outline

(defvar-local reader-outline--doc-buffername nil
  "Name of the document buffer whose outline was generated.")

(defvar-local reader-outline--doc-window nil
  "The window from where the outline was generated.")

;;;###autoload
(defun reader-outline-show ()
  "Show the document outline in a separate buffer.

The outline buffer inherits it's name from the original
document it was created from."
  (interactive)
  (unless (derived-mode-p 'reader-mode)
    (user-error "Not in a reader-mode buffer"))
  (unless reader-current-doc-outline
    (user-error "This document has no outline"))
  (let* ((outline-data reader-current-doc-outline)
         (source-buffer (current-buffer))
         (bufname (format "*Outline of %s*" (buffer-name source-buffer))))
    (with-current-buffer (get-buffer-create bufname)
      (when-let* ((emptyp (= 0 (buffer-size)))
		  (inhibit-read-only t))
        (reader-outline-mode)
	(setq reader-outline--doc-buffername (buffer-name source-buffer))
        (reader--outline-insert-outline outline-data 1)
	(goto-char (point-min))))
    (pop-to-buffer bufname)))

(defun reader--outline-insert-outline (outline level)
  "Recursively insert OUTLINE entries at LEVEL.

Each heading title is its own clickable button."
  (dolist (entry outline)
    (let ((title (plist-get entry :title))
          (page (1+ (plist-get entry :page))) ; MuPDF does 0-indexing
	  (children (plist-get entry :children)))
      ;; This cannot be part of label (title) because that will
      ;; obscure the outline TAB bindings with button bindings.
      (insert (concat (make-string level ?*) " "))
      (insert-text-button
       title
       'reader-page page
       'action #'reader-outline-goto-entry
       'follow-link t
       'help-echo "Jump to section.")
      (insert "\n")
      (if children
          (reader--outline-insert-outline children (1+ level))))))

(defun reader-outline-select-doc-window ()
  "Display and switch to the original document's window."
  (interactive)
  (select-window (if (window-valid-p reader-outline--doc-window)
		     reader-outline--doc-window
		   (get-buffer-window reader-outline--doc-buffername))))

(defvar-keymap reader-outline-mode-map
  :doc "Keymap for `reader-outline-mode'"
  "p"        #'previous-line
  "n"        #'next-line
  "o"        #'reader-outline-select-doc-window
  "q"        #'quit-window
  "RET"      #'reader-outline-visit-page
  "M-RET"    #'reader-outline-visit-page)

(define-derived-mode reader-outline-mode outline-mode "Emacs Reader Outline"
  "Major mode for navigating document outlines."
  (setq buffer-read-only t)
  (setq-local outline-regexp "^\\*+ ")
  (use-local-map reader-outline-mode-map))

(defun reader-outline-goto-entry (button)
  "Shared logic to jump to an outline BUTTON."
  (let ((page (button-get button 'reader-page)))
    (unless (numberp page)
      (user-error "Invalid outline entry: no page info"))
    (reader-outline-select-doc-window)
    (reader-goto-page page)))

(defun reader-outline-visit-page ()
  "Jump to the page at point in the associated reader buffer."
  (interactive)
  (save-excursion
    (end-of-line)   ; This is done to make sure the we always have a
    (backward-char) ; button at point.
    (reader-outline-goto-entry (button-at (point)))))

(provide 'reader-outline)
;;; reader-outline.el ends here
